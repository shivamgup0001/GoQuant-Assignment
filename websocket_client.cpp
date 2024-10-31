#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <jsoncpp/json/json.h>
#include <iostream>
#include <string>
#include <atomic>
#include <thread>
#include <chrono>

using namespace std;

typedef websocketpp::client<websocketpp::config::asio_client> client;

class WebSocketClient {
public:
    WebSocketClient(const std::string& uri) : uri(uri), is_running(false) {
        // Initialize ASIO transport
        ws_client.clear_access_channels(websocketpp::log::alevel::all);
        ws_client.set_access_channels(websocketpp::log::alevel::connect);
        ws_client.set_access_channels(websocketpp::log::alevel::disconnect);
        ws_client.init_asio();
        
        // Set message handlers
        ws_client.set_open_handler(std::bind(&WebSocketClient::on_open, this, std::placeholders::_1));
        ws_client.set_message_handler(std::bind(&WebSocketClient::on_message, this, std::placeholders::_1, std::placeholders::_2));
        ws_client.set_close_handler(std::bind(&WebSocketClient::on_close, this, std::placeholders::_1));
        ws_client.set_fail_handler(std::bind(&WebSocketClient::on_fail, this, std::placeholders::_1));
    }

    void connect() {
        websocketpp::lib::error_code ec;
        client::connection_ptr con = ws_client.get_connection(uri, ec);
        
        if (ec) {
            std::cerr << "Connection initialization error: " << ec.message() << std::endl;
            return;
        }

        ws_client.connect(con);
        is_running = true;
        
        // Start the ASIO io_service run loop in a separate thread
        asio_thread = std::thread([this]() {
            try {
                ws_client.run();
            } catch (const std::exception& e) {
                std::cerr << "Error in WebSocket run loop: " << e.what() << std::endl;
            }
        });
    }

    void stop() {
        is_running = false;
        if (ws_hdl.lock()) {
            ws_client.close(ws_hdl, websocketpp::close::status::normal, "Closing connection");
        }
        if (asio_thread.joinable()) {
            asio_thread.join();
        }
    }

    void on_open(websocketpp::connection_hdl hdl) {
        std::cout << "Connected to server" << std::endl;
        ws_hdl = hdl;

        // Subscribe to a symbol (for example, "BTC-USD")
        subscribe_to_symbol("BTC-PERPETUAL");
    }

    void on_message(websocketpp::connection_hdl hdl, client::message_ptr msg) {
        try {

            std::cout << "Received message: " << msg->get_payload() << std::endl;

            Json::Value data;
            Json::Reader reader;
            
            if (!reader.parse(msg->get_payload(), data)) {
                std::cerr << "Failed to parse message: " << msg->get_payload() << std::endl;
                return;
            }

            // Process the received message
            std::string type = data["type"].asString();
            if (type == "orderbook") {
                std::string symbol = data["symbol"].asString();
                Json::Value orderbook = data["data"];
                std::cout << "\nReceived orderbook update for " << symbol << ":\n";
                // Pretty print the orderbook data
                if (orderbook.isMember("bids") && orderbook.isMember("asks")) {
                    std::cout << "Top Bids:\n";
                    for (int i = 0; i < std::min(3, (int)orderbook["bids"].size()); i++) {
                        std::cout << "  Price: " << orderbook["bids"][i][0].asString() 
                                << " Amount: " << orderbook["bids"][i][1].asString() << "\n";
                    }
                    std::cout << "Top Asks:\n";
                    for (int i = 0; i < std::min(3, (int)orderbook["asks"].size()); i++) {
                        std::cout << "  Price: " << orderbook["asks"][i][0].asString() 
                                << " Amount: " << orderbook["asks"][i][1].asString() << "\n";
                    }
                }
            } else if (type == "success") {
                std::cout << "Success: " << data["message"].asString() << std::endl;
            } else if (type == "error") {
                std::cerr << "Error: " << data["message"].asString() << std::endl;
            } else {
                std::cout << "Received unknown message type: " << type << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Error processing message: " << e.what() << std::endl;
        }
    }

    void on_close(websocketpp::connection_hdl hdl) {
        std::cout << "Connection closed" << std::endl;
        is_running = false;
    }

    void on_fail(websocketpp::connection_hdl hdl) {
        std::cerr << "Connection failed" << std::endl;
        is_running = false;
    }

    void subscribe_to_symbol(const std::string& symbol) {
        Json::Value request;
        request["action"] = "subscribe";
        request["symbol"] = symbol;

        Json::FastWriter writer;
        std::string request_str = writer.write(request);

        // Send the subscription message to the server
        websocketpp::lib::error_code ec;
        ws_client.send(ws_hdl, request_str, websocketpp::frame::opcode::text, ec);
        if (ec) {
            std::cerr << "Send failed: " << ec.message() << std::endl;
        } else {
            std::cout << "Sent subscription request for symbol: " << symbol << std::endl;
        }
    }

    bool is_connected() const {
        return is_running;
    }

private:
    client ws_client;
    websocketpp::connection_hdl ws_hdl;
    std::string uri;
    std::atomic<bool> is_running;
    std::thread asio_thread;
};

int main() {
    std::string server_uri = "ws://localhost:9002";
    std::cout << "Creating WebSocket client..." << std::endl; 
    WebSocketClient client(server_uri);
    
    try {
        std::cout << "Connecting to " << server_uri << std::endl;
        client.connect();

        std::cout << "Client connected, entering message loop..." << std::endl;
        
        // Keep the main thread alive while receiving messages
        while (client.is_connected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "Client disconnected, stopping..." << std::endl;
        client.stop();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }

    return 0;
}