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

    // --- Helper to clean URL ---
    auto clean_url = [](std::string url) {
        // remove https:// if present
        if (url.find("https://") == 0) {
            url = url.substr(8);
        }
        // remove trailing slash
        if (!url.empty() && url.back() == '/') {
            url.pop_back();
        }
        // remove whitespace
        url.erase(remove_if(url.begin(), url.end(), ::isspace), url.end());
        return url;
    };

    std::string lr1_url = clean_url(lr1_url_env ? lr1_url_env : "lr1-service.onrender.com");
    std::string lr2_url = clean_url(lr2_url_env ? lr2_url_env : "lr2-service.onrender.com");

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

            std::string target_url = (model == "LR1") ? lr1_url : lr2_url;

            std::cout << "\n==============================" << std::endl;
            std::cout << "Request for model: " << model << std::endl;
            std::cout << "Connecting to: [" << target_url << "]" << std::endl;

            httplib::SSLClient cli(target_url.c_str(), 443);
            cli.set_read_timeout(10,0);
            cli.set_write_timeout(10,0);

            nlohmann::json payload = {{"x", x}};
            httplib::Result r;

            bool success = false;

            // --- Retry loop (up to ~30 seconds) ---
            for (int attempt = 1; attempt <= 6; attempt++) {
                std::cout << "Attempt " << attempt << " sending POST /predict..." << std::endl;

                auto start = std::chrono::steady_clock::now();

                r = cli.Post("/predict", payload.dump(), "application/json");

                auto end = std::chrono::steady_clock::now();
                auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start).count();

                if (r) {
                    std::cout << "✅ Response received!" << std::endl;
                    std::cout << "Status: " << r->status << std::endl;
                    std::cout << "Time: " << duration << " sec" << std::endl;

                    if (r->status == 200) {
                        success = true;
                        break;
                    } else {
                        std::cout << "⚠️ Non-200 status, retrying..." << std::endl;
                    }
                } else {
                    std::cout << "❌ No response (likely sleeping or timeout)" << std::endl;
                }

                std::cout << "Waiting 5 seconds before retry...\n";
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }

            // --- Final result ---
            if (success) {
                double y = nlohmann::json::parse(r->body)["y"];
                nlohmann::json response = {{"y", y}};
                task.promise.set_value(response.dump());

                std::cout << "🎉 Model responded successfully\n";
            } else {
                task.promise.set_value("{\"status\":\"model_loading\"}");
                std::cout << "⏳ Model still loading after retries\n";
            }

        } catch (std::exception& e) {
            std::cout << "❌ Exception: " << e.what() << std::endl;
            task.promise.set_value("{\"error\":\"processing failed\"}");
        } catch (...) {
            std::cout << "❌ Unknown error" << std::endl;
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