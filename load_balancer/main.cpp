#define CPPHTTPLIB_OPENSSL_SUPPORT
#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <future>
#include <cstdlib>
#include "httplib.h"
#include "json.hpp"
#include <iostream>
#include <chrono>

struct RequestTask {
    std::string body;
    std::promise<std::string> promise;
};

std::queue<RequestTask> task_queue;
std::mutex queue_mutex;
std::condition_variable cv;

// Worker thread handles prediction requests
void worker_thread() {
    const char* lr1_url_env = std::getenv("LR1_URL");
    const char* lr2_url_env = std::getenv("LR2_URL");

    std::string lr1_url = lr1_url_env ? lr1_url_env : "lr1-service.onrender.com";
    std::string lr2_url = lr2_url_env ? lr2_url_env : "lr2-service.onrender.com";

    while (true) {
        RequestTask task;
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            cv.wait(lock, [] { return !task_queue.empty(); });
            task = std::move(task_queue.front());
            task_queue.pop();
        }

        try {
            auto j = nlohmann::json::parse(task.body);
            std::string model = j["model"];
            double x = j["x"];

            // Select target model
            std::string target_url = (model == "LR1") ? lr1_url : lr2_url;

            httplib::SSLClient cli(target_url.c_str(), 443);
            cli.set_read_timeout(5,0);
            cli.set_write_timeout(5,0);

            // Headers to avoid 429 from Render edge
            httplib::Headers headers = {
                {"Content-Type", "application/json"},
                {"Accept", "application/json"},
                {"User-Agent", "Mozilla/5.0"},
                {"Connection", "keep-alive"}
            };

            // --- Step 1: Wake container if asleep ---
            bool awake = false;
            for (int attempt = 1; attempt <= 4; ++attempt) {
                std::cout << "[Wake attempt " << attempt << "] GET /\n";
                auto wake = cli.Get("/", headers);
                if (wake && wake->status == 200) {
                    std::cout << "✅ Model " << model << " is awake (status " << wake->status << ")\n";
                    awake = true;
                    break;
                } else {
                    std::cout << "⚠️ Wake failed or not ready, retrying in 15s...\n";
                    std::this_thread::sleep_for(std::chrono::seconds(15));
                }
            }

            if (!awake) {
                std::cout << "⚠️ Model " << model << " did not wake after retries\n";
                task.promise.set_value("{\"status\":\"model_loading\"}");
                continue;
            }

            // --- Step 2: Send actual prediction POST ---
            nlohmann::json payload = {{"x", x}};
            auto r = cli.Post("/predict", headers, payload.dump(), "application/json");

            // --- Step 3: Respond to frontend ---
            if (r && r->status == 200) {
                double y = nlohmann::json::parse(r->body)["y"];
                nlohmann::json response = {{"y", y}};
                task.promise.set_value(response.dump());
                std::cout << "✅ Prediction done: " << y << "\n";
            } else {
                std::cout << "⚠️ Prediction failed, model may still be loading\n";
                task.promise.set_value("{\"status\":\"model_loading\"}");
            }

        } catch (const std::exception& e) {
            std::cout << "❌ Exception in worker: " << e.what() << "\n";
            task.promise.set_value("{\"error\":\"processing failed\"}");
        }
    }
}

int main() {
    httplib::Server svr;

    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 8080;

    std::thread(worker_thread).detach();

    // CORS preflight
    svr.Options("/predict", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    // POST endpoint
    svr.Post("/predict", [](const httplib::Request& req, httplib::Response& res) {
        std::promise<std::string> promise;
        std::future<std::string> future = promise.get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            task_queue.push({req.body, std::move(promise)});
        }

        cv.notify_one();

        // Wait up to 10 seconds for worker thread
        if (future.wait_for(std::chrono::seconds(10)) == std::future_status::ready) {
            res.set_content(future.get(), "application/json");
        } else {
            res.set_content("{\"status\":\"model_loading\"}", "application/json");
        }

        res.set_header("Access-Control-Allow-Origin", "*");
    });

    std::cout << "Load balancer running on port " << port << "...\n";
    svr.listen("0.0.0.0", port);
}