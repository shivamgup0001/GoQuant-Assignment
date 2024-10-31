#include <iostream>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <set>
#include <map>
#include <thread>
#include <mutex>
#include <jsoncpp/json/json.h>
#include <curl/curl.h>

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

typedef websocketpp::server<websocketpp::config::asio> server;
typedef server::message_ptr message_ptr;
typedef websocketpp::connection_hdl connection_hdl;

// Callback for handling CURL response data
size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t totalSize = size * nmemb;
    s->append(static_cast<char*>(contents), totalSize);
    return totalSize;
}

class DeribitAPI {
private:
    std::string test_url = "https://test.deribit.com/api/v2/";
    CURL* curl;
    std::string api_key;
    std::string api_secret;
    std::string access_token;

    bool authenticate() {
        if (!curl) {
            std::cerr << "Failed to initialize CURL" << std::endl;
            return false;
        }

        Json::Value request;
        request["jsonrpc"] = "2.0";
        request["id"] = "auth";
        request["method"] = "public/auth";
        
        Json::Value params;
        params["grant_type"] = "client_credentials";
        params["client_id"] = api_key;
        params["client_secret"] = api_secret;
        request["params"] = params;

        Json::FastWriter writer;
        std::string request_str = writer.write(request);

        std::string response;
        struct curl_slist* headers = curl_slist_append(NULL, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, (test_url + "public/auth").c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            std::cerr << "Authentication failed: " << curl_easy_strerror(res) << std::endl;
            return false;
        }

        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root) || !root["result"].isMember("access_token")) {
            std::cerr << "Failed to parse authentication response" << std::endl;
            return false;
        }

        access_token = root["result"]["access_token"].asString();
        return true;
    }

public:
    DeribitAPI(const std::string& key, const std::string& secret) 
        : api_key(key), api_secret(secret) {
        curl = curl_easy_init();
        authenticate();
    }

    ~DeribitAPI() {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }

    Json::Value sendRequest(const std::string& method, const Json::Value& params) {
        Json::Value request;
        request["jsonrpc"] = "2.0";
        request["id"] = "1";
        request["method"] = method;
        request["params"] = params;

        Json::FastWriter writer;
        std::string request_str = writer.write(request);

        std::string response;
        struct curl_slist* headers = curl_slist_append(NULL, ("Authorization: Bearer " + access_token).c_str());
        curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, (test_url + method).c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            std::cerr << "Request failed: " << curl_easy_strerror(res) << std::endl;
            return Json::Value();
        }

        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            std::cerr << "Failed to parse response" << std::endl;
            return Json::Value();
        }

        return root["result"];
    }

    Json::Value getOrderbook(const std::string& symbol) {
        Json::Value params;
        params["instrument_name"] = symbol;
        Json::Value result = sendRequest("public/get_order_book", params);
        
        // Add debug logging
        std::cout << "Fetched orderbook for " << symbol << std::endl;
        if (!result.isNull()) {
            std::cout << "Orderbook data received from API" << std::endl;
        } else {
            std::cout << "No orderbook data received from API" << std::endl;
        }
        
        return result;
    }
};

class DeribitWebSocketServer {
private:
    server ws_server;
    std::set<connection_hdl, std::owner_less<connection_hdl>> connections;
    std::map<connection_hdl, std::set<std::string>, std::owner_less<connection_hdl>> subscriptions;
    std::map<std::string, Json::Value> orderbooks;
    std::mutex mutex;
    bool running;
    std::thread update_thread;

    DeribitAPI& api;

    void on_open(connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(mutex);
        connections.insert(hdl);
        std::cout << "New connection opened" << std::endl;
    }

    void on_close(connection_hdl hdl) {
        std::lock_guard<std::mutex> lock(mutex);
        connections.erase(hdl);
        subscriptions.erase(hdl);
        std::cout << "Connection closed" << std::endl;
    }

    void on_message(connection_hdl hdl, message_ptr msg) {
        Json::Value request;
        Json::Reader reader;

        if (!reader.parse(msg->get_payload(), request) || 
            !request.isMember("action") || !request.isMember("symbol")) {
            send_error(hdl, "Invalid JSON format or missing required fields: action and symbol");
            return;
        }

        std::string action = request["action"].asString();
        std::string symbol = request["symbol"].asString();

        if (action == "subscribe") {
            handle_subscribe(hdl, symbol);
        } else if (action == "unsubscribe") {
            handle_unsubscribe(hdl, symbol);
        } else {
            send_error(hdl, "Invalid action. Supported actions: subscribe, unsubscribe");
        }
    }

    void handle_subscribe(connection_hdl hdl, const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex);
        std::cout << "Handling subscription for symbol: " << symbol << std::endl;
        
        subscriptions[hdl].insert(symbol);

        Json::Value orderBookData = api.getOrderbook(symbol);

        std::cout << "Sending initial orderbook update to new subscriber" << std::endl;
        send_orderbook_update(hdl, symbol, orderBookData);
        send_success(hdl, "Subscribed to " + symbol);
    }

    void handle_unsubscribe(connection_hdl hdl, const std::string& symbol) {
        std::lock_guard<std::mutex> lock(mutex);
        subscriptions[hdl].erase(symbol);
        send_success(hdl, "Unsubscribed from " + symbol);
    }

    void send_error(connection_hdl hdl, const std::string& message) {
        Json::Value response;
        response["type"] = "error";
        response["message"] = message;

        Json::FastWriter writer;
        ws_server.send(hdl, writer.write(response), websocketpp::frame::opcode::text);
    }

    void send_success(connection_hdl hdl, const std::string& message) {
        Json::Value response;
        response["type"] = "success";
        response["message"] = message;

        Json::FastWriter writer;
        ws_server.send(hdl, writer.write(response), websocketpp::frame::opcode::text);
    }

    void send_orderbook_update(connection_hdl hdl, const std::string& symbol, const Json::Value& orderbook) {
        try {
            Json::Value response;
            response["type"] = "orderbook";
            response["symbol"] = symbol;
            response["data"] = orderbook;

            Json::FastWriter writer;
            std::string message = writer.write(response);
            
            std::cout << "Sending orderbook update: " << message << std::endl;
            
            ws_server.send(hdl, message, websocketpp::frame::opcode::text);
            std::cout << "Orderbook update sent successfully" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error sending orderbook update: " << e.what() << std::endl;
            throw;
        }
    }

    void update_loop() {
        while (running) {
            try {
                std::lock_guard<std::mutex> lock(mutex);
                std::set<std::string> symbols;

                // Collect all unique symbols from active subscriptions
                for (const auto& sub : subscriptions) {
                    symbols.insert(sub.second.begin(), sub.second.end());
                }

                // Update orderbooks for each subscribed symbol
                for (const std::string& symbol : symbols) {
                    std::cout << "Updating orderbook for symbol: " << symbol << std::endl;
                    Json::Value newOrderbook;

                    newOrderbook = api.getOrderbook(symbol);
                    std::cout << "Sending orderbook update to all subscribers" << std::endl;
                    for (const auto& sub : subscriptions) {
                        if (sub.second.find(symbol) != sub.second.end()) {
                            try {
                                send_orderbook_update(sub.first, symbol, newOrderbook);
                            } catch (const std::exception& e) {
                                std::cerr << "Error sending update to client: " << e.what() << std::endl;
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Error in update loop: " << e.what() << std::endl;
            }

            // Sleep for a short duration to avoid overwhelming the API
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));  // Increased sleep time for testing
        }
    }

public:
    DeribitWebSocketServer(DeribitAPI& deribit_api, uint16_t port = 9002) 
        : api(deribit_api), running(false) {
        try {
            ws_server.clear_access_channels(websocketpp::log::alevel::all);
            ws_server.set_access_channels(websocketpp::log::alevel::connect);
            ws_server.set_access_channels(websocketpp::log::alevel::disconnect);
            
            ws_server.init_asio();
            ws_server.set_open_handler(bind(&DeribitWebSocketServer::on_open, this, ::_1));
            ws_server.set_close_handler(bind(&DeribitWebSocketServer::on_close, this, ::_1));
            ws_server.set_message_handler(bind(&DeribitWebSocketServer::on_message, this, ::_1, ::_2));
            
            ws_server.set_reuse_addr(true);
            ws_server.listen(port);
            std::cout << "WebSocket server listening on port " << port << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "Error initializing server: " << e.what() << std::endl;
            throw;
        }
    }

    void run() {
        try {
            running = true;
            update_thread = std::thread(&DeribitWebSocketServer::update_loop, this);
            ws_server.start_accept();
            ws_server.run();
        } catch (const std::exception& e) {
            std::cerr << "Error in server run: " << e.what() << std::endl;
            stop();
            throw;
        }
    }

    void stop() {
        running = false;
        if (update_thread.joinable()) {
            update_thread.join();
        }
        ws_server.stop();
    }
    ~DeribitWebSocketServer() {
        stop();
    }
};

int main() {
    std::string key = "9IpVT2Qk";
    std::string secret = "nj323UWlhnB6DYmwPaav3o_zI91q__smQde0LhTArRc";
    DeribitAPI api(key, secret);

    DeribitWebSocketServer ws_server(api);
    ws_server.run();

    return 0;
}
