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
#include <map>

struct RequestTask {
    std::string body;
    std::promise<std::string> promise;
};

std::queue<RequestTask> task_queue;
std::mutex queue_mutex;
std::condition_variable cv;

// --- Model state tracking ---
enum ModelState { READY, WAKING };
std::map<std::string, ModelState> model_state = {
    {"LR1", READY},
    {"LR2", READY}
};

std::mutex model_mutex;
std::condition_variable model_cv;

// --- URL cleaner ---
std::string clean_url(std::string url) {
    if (url.find("https://") == 0) url = url.substr(8);
    if (!url.empty() && url.back() == '/') url.pop_back();
    url.erase(remove_if(url.begin(), url.end(), ::isspace), url.end());
    return url;
}

bool try_request(httplib::SSLClient &cli, const nlohmann::json &payload, httplib::Result &r) {
    r = cli.Post("/predict", payload.dump(), "application/json");

    if (!r) {
        std::cout << "❌ No response\n";
        return false;
    }

    std::cout << "Status: " << r->status << std::endl;

    if (r->status == 200) return true;

    if (r->status == 429) {
        std::cout << "🚫 Rate limited → sleeping 20s\n";
        std::this_thread::sleep_for(std::chrono::seconds(20));
        return false;
    }

    std::this_thread::sleep_for(std::chrono::seconds(10));
    return false;
}

void worker_thread() {
    const char* lr1_env = std::getenv("LR1_URL");
    const char* lr2_env = std::getenv("LR2_URL");

    std::string lr1_url = clean_url(lr1_env ? lr1_env : "lr1-service.onrender.com");
    std::string lr2_url = clean_url(lr2_env ? lr2_env : "lr2-service.onrender.com");

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

            std::string url = (model == "LR1") ? lr1_url : lr2_url;

            std::cout << "\n=== REQUEST ===\n";
            std::cout << "Model: " << model << "\nURL: " << url << "\n";

            httplib::SSLClient cli(url.c_str(), 443);
            cli.set_read_timeout(15,0);
            cli.set_write_timeout(15,0);

            nlohmann::json payload = {{"x", x}};
            httplib::Result r;

            // --- MODEL LOCK ---
            {
                std::unique_lock<std::mutex> lock(model_mutex);

                if (model_state[model] == WAKING) {
                    std::cout << "⏳ Model already waking, waiting...\n";
                    model_cv.wait(lock, [&] { return model_state[model] == READY; });
                    std::cout << "✅ Model ready (wait finished)\n";
                } else {
                    std::cout << "🚀 This thread will wake model\n";
                    model_state[model] = WAKING;
                }
            }

            bool success = false;

            // --- ONLY ONE THREAD DOES THIS ---
            {
                std::unique_lock<std::mutex> lock(model_mutex, std::defer_lock);
                lock.lock();

                if (model_state[model] == WAKING) {
                    lock.unlock();

                    std::cout << "🔥 Waking model (slow retry loop)\n";

                    for (int i = 1; i <= 4; i++) {
                        std::cout << "Wake attempt " << i << "\n";

                        if (try_request(cli, payload, r)) {
                            success = true;
                            break;
                        }
                    }

                    lock.lock();
                    model_state[model] = READY;
                    lock.unlock();

                    model_cv.notify_all();
                } else {
                    lock.unlock();
                }
            }

            // --- If not success yet, try once normally ---
            if (!success) {
                std::cout << "🔁 Final attempt after wake\n";
                success = try_request(cli, payload, r);
            }

            // --- RESPONSE ---
            if (success) {
                double y = nlohmann::json::parse(r->body)["y"];
                task.promise.set_value(nlohmann::json{{"y", y}}.dump());
                std::cout << "🎉 SUCCESS\n";
            } else {
                task.promise.set_value("{\"status\":\"model_loading\"}");
                std::cout << "⏳ STILL LOADING\n";
            }

        } catch (std::exception &e) {
            std::cout << "❌ Exception: " << e.what() << "\n";
            task.promise.set_value("{\"error\":\"processing failed\"}");
        }
    }
}

int main() {
    httplib::Server svr;

    const char* port_env = std::getenv("PORT");
    int port = port_env ? std::stoi(port_env) : 8080;

    // Multiple workers (important)
    for (int i = 0; i < 3; i++) {
        std::thread(worker_thread).detach();
    }

    svr.Options("/predict", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
    });

    svr.Post("/predict", [](const httplib::Request& req, httplib::Response& res) {
        std::promise<std::string> promise;
        auto future = promise.get_future();

        {
            std::lock_guard<std::mutex> lock(queue_mutex);
            task_queue.push({req.body, std::move(promise)});
        }

        cv.notify_one();

        // wait longer (important!)
        if (future.wait_for(std::chrono::seconds(40)) == std::future_status::ready) {
            res.set_content(future.get(), "application/json");
        } else {
            res.set_content("{\"status\":\"model_loading\"}", "application/json");
        }

        res.set_header("Access-Control-Allow-Origin", "*");
    });

    std::cout << "Load balancer running on port " << port << "...\n";
    svr.listen("0.0.0.0", port);
}