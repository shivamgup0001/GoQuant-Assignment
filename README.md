# Deribit Trading API Client

This repository contains a C++ implementation of a trading client for the Deribit cryptocurrency derivatives exchange. It includes both REST API and WebSocket functionality for real-time market data and trading operations.

## Features

- REST API client for trading operations
- WebSocket server for real-time orderbook updates
- WebSocket client for consuming market data
- Support for authentication and private endpoints
- Order management (place, modify, cancel orders)
- Account information retrieval
- Real-time orderbook data streaming

## Prerequisites

- C++ compiler with C++11 support
- libcurl
- JsonCpp
- WebSocket++ library
- ASIO (included with WebSocket++)

## Components

### 1. REST API Client (main.cpp)
- Handles authentication with Deribit
- Implements trading operations:
  - Place buy/sell orders
  - Modify existing orders
  - Cancel orders
  - Get account summary
  - Retrieve orderbook data
  - Get positions

### 2. WebSocket Server (websocket_server.cpp)
- Provides real-time orderbook updates
- Handles client subscriptions
- Manages multiple client connections
- Implements pub/sub pattern for market data

### 3. WebSocket Client (websocket_client.cpp)
- Connects to the WebSocket server
- Subscribes to market data
- Processes and displays real-time orderbook updates

## Building the Project

```bash
# Install dependencies
sudo apt-get install libcurl4-openssl-dev libjsoncpp-dev

# Clone the repository
git clone [repository-url]
cd deribit-trading-client

# Build the project
g++ main.cpp -lcurl -ljsoncpp -o deribit_client
g++ websocket_server.cpp -lcurl -ljsoncpp -lwebsocketpp -lpthread -o ws_server
g++ websocket_client.cpp -lwebsocketpp -ljsoncpp -lpthread -o ws_client
