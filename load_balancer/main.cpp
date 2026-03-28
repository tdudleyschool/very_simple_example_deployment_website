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

// Track backend ready
bool backend_ready = false;

void worker_thread() {
    const char* lr1_url_env = std::getenv("LR1_URL");
    const char* lr2_url_env = std::getenv("LR2_URL");

    std::string lr1_url = lr1_url_env ? lr1_url_env : "http://localhost:8001";
    std::string lr2_url = lr2_url_env ? lr2_url_env : "http://localhost:8002";

    backend_ready = true; // mark backend ready after worker thread starts

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
            double y = 0;

            httplib::Client* cli = nullptr;
            if (model == "LR1") {
                cli = new httplib::Client(lr1_url.c_str());
            } else if (model == "LR2") {
                cli = new httplib::Client(lr2_url.c_str());
            }

            if (cli) {
                nlohmann::json payload = {{"x", x}};
                auto r = cli->Post("/predict", payload.dump(), "application/json");
                if (r && r->status == 200) {
                    y = nlohmann::json::parse(r->body)["y"];
                } else {
                    // timeout or error: respond model_loading
                    task.promise.set_value("{\"status\":\"model_loading\"}");
                    delete cli;
                    continue;
                }
                delete cli;
            }

            nlohmann::json response = {{"status", "ok"}, {"y", y}};
            task.promise.set_value(response.dump());

        } catch (...) {
            task.promise.set_value("{\"status\":\"error\",\"message\":\"processing failed\"}");
        }
    }
}

int main() {
    httplib::Server svr;

    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 8080;

    std::thread(worker_thread).detach();

    // -------------------
    // CORS preflight
    // -------------------
    svr.Options("/predict", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    // -------------------
    // /ready endpoint
    // -------------------
    svr.Get("/ready", [](const httplib::Request&, httplib::Response& res) {
        nlohmann::json response = {{"status", backend_ready ? "ok" : "starting"}};
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(response.dump(), "application/json");
    });

    // -------------------
    // /predict endpoint
    // -------------------
    svr.Post("/predict", [](const httplib::Request& req, httplib::Response& res) {
        std::promise<std::string> promise;
        std::future<std::string> future = promise.get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            task_queue.push({req.body, std::move(promise)});
        }

        cv.notify_one();

        // ✅ wait for worker, timeout 5s
        if (future.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
            nlohmann::json timeout_resp = {{"status","model_loading"}};
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(timeout_resp.dump(), "application/json");
        } else {
            std::string result = future.get();
            res.set_header("Access-Control-Allow-Origin", "*");
            res.set_content(result, "application/json");
        }
    });

    std::cout << "Load balancer running on port " << port << "...\n";
    svr.listen("0.0.0.0", port);
}