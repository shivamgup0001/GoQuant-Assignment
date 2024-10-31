#include <iostream>
#include <curl/curl.h>
#include <jsoncpp/json/json.h>

using namespace std;

size_t WriteCallback(void* contents, size_t size, size_t nmemb, string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

class DeribitAPI {
private:
    string test_url = "https://test.deribit.com/api/v2/";
    CURL* curl;
    string api_key;
    string api_secret;
    string access_token;

    bool authenticate() {
        if (!curl) {
            cerr << "Failed to initialize CURL" << endl;
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
        string request_str = writer.write(request);

        string response;
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        curl_easy_setopt(curl, CURLOPT_URL, (test_url + "public/auth").c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            cerr << "Authentication failed: " << curl_easy_strerror(res) << endl;
            return false;
        }

        Json::Value root;
        Json::Reader reader;
        bool parsing_successful = reader.parse(response, root);

        if (!parsing_successful) {
            cerr << "Failed to parse authentication response" << endl;
            return false;
        }

        if (root.isMember("result") && root["result"].isMember("access_token")) {
            access_token = root["result"]["access_token"].asString();
            cout << "Authentication successful!" << endl;
            return true;
        } else {
            cerr << "Authentication error: " << response << endl;
            return false;
        }
    }

public:
    DeribitAPI(const string& key, const string& secret) 
        : api_key(key), api_secret(secret) {
        curl = curl_easy_init();

        // Set up TCP keep-alive options for lower latency in persistent connections
        if (curl) {
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPIDLE, 120L);
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPINTVL, 60L); 
        }

        authenticate();
    }

    ~DeribitAPI() {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }

    Json::Value sendRequest(const string& method, const Json::Value& params) {
        if (access_token.empty() && method.find("private/") == 0) {
            if (!authenticate()) {
                return Json::Value();
            }
        }

        Json::Value request;
        request["jsonrpc"] = "2.0";
        request["id"] = "1";
        request["method"] = method;
        request["params"] = params;

        Json::FastWriter writer;
        string request_str = writer.write(request);

        string response;
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        if (!access_token.empty()) {
            headers = curl_slist_append(headers, ("Authorization: Bearer " + access_token).c_str());
        }

        curl_easy_setopt(curl, CURLOPT_URL, (test_url + method).c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request_str.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            cerr << "Request failed: " << curl_easy_strerror(res) << endl;
            return Json::Value();
        }

        Json::Value root;
        Json::Reader reader;
        if (!reader.parse(response, root)) {
            cerr << "Failed to parse response" << endl;
            return Json::Value();
        }

        return root;
    }

    // Place a buy order
    Json::Value placeBuyOrder(const string& instrument, const string& type, double amount, double price) {
        Json::Value params;
        params["instrument_name"] = instrument;
        params["amount"] = amount;
        params["price"] = price;
        params["type"] = type;
        
        return sendRequest("private/buy", params);
    }

    // Place a sell order
    Json::Value placeSellOrder(const string& instrument, const string& type, double amount, double price) {
        Json::Value params;
        params["instrument_name"] = instrument;
        params["amount"] = amount;
        params["price"] = price;
        params["type"] = type;
        
        return sendRequest("private/sell", params);
    }

    // Cancel an order
    Json::Value cancelOrder(const string& orderId) {
        Json::Value params;
        params["order_id"] = orderId;
        
        return sendRequest("private/cancel", params);
    }

    // Cancel all orders
    Json::Value cancelAllOrders() {
        Json::Value params;
        return sendRequest("private/cancel_all", params);
    }

    // Modify an order
    Json::Value modifyOrder(const string& orderId, double newAmount = 0.0, double newPrice = 0.0,double contractSize=0.0,const string& instrument="") {
        Json::Value params;
        params["order_id"] = orderId;
        
        // Check if newAmount is a multiple of contractSize
        int multiple = static_cast<int>(newAmount / contractSize);
        double adjustedAmount = multiple * contractSize;
        
        // Only update if adjustedAmount differs from newAmount
        if (adjustedAmount != newAmount) {
            newAmount = adjustedAmount;
        }

        if (newAmount > 0.0) {
            params["amount"] = newAmount;
            //params["contracts"] = newAmount;
        }

        if (newPrice > 0.0) {
            params["price"] = newPrice;
        }
        //Add instrument_name if required by your API
        params["instrument_name"] = instrument;
        
        return sendRequest("private/edit", params);
    }

    // Get order state
    Json::Value getOrderState(const string& orderId) {
        Json::Value params;
        params["order_id"] = orderId;
        
        return sendRequest("private/get_order_state", params);
    }

    // Get open orders
    Json::Value getOpenOrders(const string& instrument) {
        Json::Value params;
        params["instrument_name"] = instrument;
        
        return sendRequest("private/get_open_orders_by_instrument", params);
    }

    Json::Value getAccountSummary() {
        Json::Value params;
        params["extended"] = true;
        return sendRequest("private/get_account_summary", params);
    }

    Json::Value getOrderbook(const string& instrument) {
        Json::Value params;
        params["instrument_name"] = instrument;
        return sendRequest("public/get_order_book", params);
    }

    Json::Value getCurrentPositions(const string& currency) {
        Json::Value params;
        params["currency"] = currency;
        return sendRequest("private/get_positions", params);
    }

    Json::Value getSymbolInfo(const string& symbol) {
        Json::Value params;
        params["instrument_name"] = symbol;
        return sendRequest("public/get_contract_size", params);
    }

};

int main() {
    curl_global_init(CURL_GLOBAL_ALL);

    DeribitAPI api("9IpVT2Qk", "nj323UWlhnB6DYmwPaav3o_zI91q__smQde0LhTArRc");

    // Example usage:
    
    // Place a buy order
    Json::Value buyOrderResponse = api.placeBuyOrder("BTC-PERPETUAL", "limit", 10, 50000.0);
    cout << "Buy Order Response: " << buyOrderResponse << endl;

    // Then call it
    Json::Value symbolInfo = api.getSymbolInfo("BTC-PERPETUAL"); // Replace with your symbol
    double contractSize = symbolInfo["result"]["contract_size"].asDouble();
    // If order was placed successfully, get its ID from the response
    string orderId;
    if (buyOrderResponse.isMember("result") && buyOrderResponse["result"].isMember("order")) {
        orderId = buyOrderResponse["result"]["order"]["order_id"].asString();
        
        //Modify the order
        // Json::Value modifyResponse = api.modifyOrder(orderId, 20.0, 51000.0,contractSize,"BTC-PERPETUAL"); // Change price to 51000
        // cout << "Modify Order Response: " << modifyResponse << endl;

        // Get order state
        Json::Value orderState = api.getOrderState(orderId);
        cout << "Order State: " << orderState << endl;

        //Cancel the order
        Json::Value cancelResponse = api.cancelOrder(orderId);
        cout << "Cancel Order Response: " << cancelResponse << endl;
    }

    // Get open orders
    Json::Value openOrders = api.getOpenOrders("BTC-PERPETUAL");
    cout << "Open Orders: " << openOrders << endl;

    // Get current positions example
    Json::Value positionsResponse = api.getCurrentPositions("BTC");
    cout << "Positions: " << positionsResponse << endl;

    // Get account summary
    Json::Value accountSummary = api.getAccountSummary();
    cout << "Account Summary: " << accountSummary << endl;

    curl_global_cleanup();
    return 0;
}