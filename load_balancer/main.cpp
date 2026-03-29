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

            // Select correct model URL
            std::string target_url = (model == "LR1") ? lr1_url : lr2_url;

            httplib::SSLClient cli(target_url.c_str(), 443);
            cli.set_read_timeout(5,0);
            cli.set_write_timeout(5,0);

            nlohmann::json payload = {{"x", x}};
            httplib::Result r;

            // --- Step 1: Wake up model if needed ---
            auto warmup = cli.Get("/predict");
            if (!warmup) {
                std::cout << "Model " << model << " may be asleep, waking up..." << std::endl;
                std::this_thread::sleep_for(std::chrono::seconds(2));
                warmup = cli.Get("/predict");
            }

            // --- Step 2: Send actual prediction ---
            r = cli.Post("/predict", payload.dump(), "application/json");

            // --- Step 3: Respond to frontend ---
            if (r && r->status == 200) {
                double y = nlohmann::json::parse(r->body)["y"];
                nlohmann::json response = {{"y", y}};
                task.promise.set_value(response.dump());
            } else {
                // Model didn't respond in time
                task.promise.set_value("{\"status\":\"model_loading\"}");
            }

        } catch (...) {
            task.promise.set_value("{\"error\":\"processing failed\"}");
        }
    }
}

int main() {
    httplib::Server svr;

    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 8080;

    std::thread(worker_thread).detach();

    svr.Options("/predict", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    svr.Post("/predict", [](const httplib::Request& req, httplib::Response& res) {
        std::promise<std::string> promise;
        std::future<std::string> future = promise.get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            task_queue.push({req.body, std::move(promise)});
        }

        cv.notify_one();

        // Wait up to 5 seconds for worker thread
        if (future.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
            res.set_content(future.get(), "application/json");
        } else {
            res.set_content("{\"status\":\"model_loading\"}", "application/json");
        }

        res.set_header("Access-Control-Allow-Origin", "*");
    });

    std::cout << "Load balancer running on port " << port << "..." << std::endl;
    svr.listen("0.0.0.0", port);
}