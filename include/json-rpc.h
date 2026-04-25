/*
Plugin Name
Copyright (C) <2026> <MiraBox> <Email Address>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#pragma once

#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <cstdint>

namespace streamdock {
namespace jsonrpc {

    // JSON-RPC 2.0 错误代码
    enum error_codes : int64_t {
        PARSE_ERROR      = -32700,
        INTERNAL_ERROR   = -32603,
        INVALID_PARAMS   = -32602,
        METHOD_NOT_FOUND = -32601,
        INVALID_REQUEST  = -32600,
        SERVER_ERROR_MAX = -32099,
        SERVER_ERROR     = -32000,
    };

    // JSON-RPC 错误基类
    class error : public std::runtime_error {
    public:
        error(const char* reason) : std::runtime_error(reason) {}
        virtual ~error() = default;

        virtual int64_t id() const {
            return 0;
        }
    };

    // 各种错误类型
    class parse_error : public error {
    public:
        parse_error(const char* reason) : error(reason) {}
        int64_t id() const override {
            return static_cast<int64_t>(error_codes::PARSE_ERROR);
        }
    };

    class invalid_params_error : public error {
    public:
        invalid_params_error(const char* reason) : error(reason) {}
        int64_t id() const override {
            return static_cast<int64_t>(error_codes::INVALID_PARAMS);
        }
    };

    class invalid_request_error : public error {
    public:
        invalid_request_error(const char* reason) : error(reason) {}
        int64_t id() const override {
            return static_cast<int64_t>(error_codes::INVALID_REQUEST);
        }
    };

    class method_not_found_error : public error {
    public:
        method_not_found_error(const char* reason) : error(reason) {}
        int64_t id() const override {
            return static_cast<int64_t>(error_codes::METHOD_NOT_FOUND);
        }
    };

    class internal_error : public error {
    public:
        internal_error(const char* reason) : error(reason) {}
        int64_t id() const override {
            return static_cast<int64_t>(error_codes::INTERNAL_ERROR);
        }
    };

    class server_error : public error {
        int64_t _id;
    public:
        server_error(const char* reason, int64_t rid) : error(reason), _id(rid) {}
        int64_t id() const override {
            return static_cast<int64_t>(error_codes::SERVER_ERROR) - _id;
        }
    };

    class custom_error : public error {
        int64_t _id;
    public:
        custom_error(const char* reason, int64_t rid) : error(reason), _id(rid) {}
        int64_t id() const override {
            return _id;
        }
    };

    // 客户端信息类
    class client {
        std::string _remote_version;
    public:
        client() = default;
        void set_version(const std::string& version) {
            _remote_version = version;
        }
        std::string get_version() const {
            return _remote_version;
        }
    };

    // JSON-RPC 基类
    class jsonrpc {
    protected:
        virtual ~jsonrpc() = default;
        jsonrpc() = default;

        nlohmann::json _json;
        client*        _client = nullptr;

    public:
        jsonrpc& clear_id();
        jsonrpc& set_id();
        jsonrpc& set_id(int64_t value);
        jsonrpc& set_id(const std::string& value);
        bool     get_id(int64_t& value);
        bool     get_id(std::string& value);
        bool     has_id();
        jsonrpc& copy_id(jsonrpc& other);
        client*  get_client();

        nlohmann::json compile();

    public:
        virtual void validate() = 0;
    };

    // JSON-RPC 请求类
    class request : public jsonrpc {
    public:
        virtual ~request() = default;
        request() = default;
        request(nlohmann::json& json, client* c);

        request&    set_method(const std::string& value);
        bool        get_method(std::string& value);
        std::string get_method();

        request& clear_params();
        request& set_params(nlohmann::json value);
        bool     get_params(nlohmann::json& value);

    public:
        void validate() override;
    };

    // JSON-RPC 响应类
    class response : public jsonrpc {
    public:
        virtual ~response() = default;
        response() = default;
        response(nlohmann::json& json, client* c);

        response& set_result(nlohmann::json value);
        bool      get_result(nlohmann::json& value);

        response& set_error(int64_t code, const std::string& message);
        response& set_error(int64_t code, const std::string& message, nlohmann::json data);
        bool      get_error_code(int64_t& value);
        bool      get_error_message(std::string& value);
        bool      get_error_data(nlohmann::json& value);

    public:
        void validate() override;
    };

} // namespace jsonrpc
} // namespace streamdock
