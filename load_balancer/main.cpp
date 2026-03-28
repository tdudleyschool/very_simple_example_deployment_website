#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <future>
#include "httplib.h"
#include "json.hpp"
#include <iostream>

struct RequestTask {
    std::string body;
    std::promise<std::string> promise;
};

std::queue<RequestTask> task_queue;
std::mutex queue_mutex;
std::condition_variable cv;

void worker_thread() {
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

            if (model == "LR1") {
                httplib::Client cli("lr1_service", 8001);
                nlohmann::json payload = {{"x", x}};
                auto r = cli.Post("/predict", payload.dump(), "application/json");

                if (r && r->status == 200) {
                    y = nlohmann::json::parse(r->body)["y"];
                }
            }
            else if (model == "LR2") {
                httplib::Client cli("lr2_service", 8002);
                nlohmann::json payload = {{"x", x}};
                auto r = cli.Post("/predict", payload.dump(), "application/json");

                if (r && r->status == 200) {
                    y = nlohmann::json::parse(r->body)["y"];
                }
            }

            nlohmann::json response = {{"y", y}};
            task.promise.set_value(response.dump());

        } catch (...) {
            task.promise.set_value("{\"error\":\"processing failed\"}");
        }
    }
}

int main() {
    httplib::Server svr;

    std::thread(worker_thread).detach();

    // ✅ CORS preflight
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

        // ✅ WAIT for worker result
        std::string result = future.get();

        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_content(result, "application/json");
    });

    std::cout << "Load balancer (queue-based) running on port 8080..." << std::endl;
    svr.listen("0.0.0.0", 8080);
}