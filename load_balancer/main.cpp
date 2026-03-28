#include <queue>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <future>
#include <cstdlib>   // for getenv from render
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
    // !!! Read model URLs from environment variables
    const char* lr1_url_env = std::getenv("LR1_URL");
    const char* lr2_url_env = std::getenv("LR2_URL");

    std::string lr1_url = lr1_url_env ? lr1_url_env : "http://localhost:8001";
    std::string lr2_url = lr2_url_env ? lr2_url_env : "http://localhost:8002";

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
                //!!!need real render hostname. i think it has to match the name on render.
                httplib::Client cli(lr1_url.c_str());
                nlohmann::json payload = {{"x", x}};
                auto r = cli.Post("/predict", payload.dump(), "application/json");

                if (r && r->status == 200) {
                    y = nlohmann::json::parse(r->body)["y"];
                }
            }
            else if (model == "LR2") {
                //!!!need real render hostname. i think it has to match the name on render. Get from env variables

                httplib::Client cli(lr2_url.c_str());
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

    //!!! For render Dynamic port from Render
    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 8080;

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

   //!!! REPLACED PORT to use Render env variable if no env prot then just listen at 8080
    std::cout << "Load balancer running on port " << port << "..." << std::endl;
    svr.listen("0.0.0.0", port);
}