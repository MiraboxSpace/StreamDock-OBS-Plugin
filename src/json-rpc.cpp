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

#include "json-rpc.h"
#include <obs-module.h>
#include <plugin-support.h>

namespace streamdock {
namespace jsonrpc {

    // === jsonrpc 基类实现 ===

    jsonrpc& jsonrpc::clear_id() {
        if (_json.contains("id")) {
            _json.erase("id");
        }
        return *this;
    }

    jsonrpc& jsonrpc::set_id() {
        _json["id"] = nullptr;
        return *this;
    }

    jsonrpc& jsonrpc::set_id(int64_t value) {
        _json["id"] = value;
        return *this;
    }

    jsonrpc& jsonrpc::set_id(const std::string& value) {
        _json["id"] = value;
        return *this;
    }

    bool jsonrpc::get_id(int64_t& value) {
        if (!_json.contains("id")) {
            return false;
        }
        try {
            if (_json["id"].is_number_integer()) {
                value = _json["id"].get<int64_t>();
                return true;
            }
        } catch (...) {
            return false;
        }
        return false;
    }

    bool jsonrpc::get_id(std::string& value) {
        if (!_json.contains("id")) {
            return false;
        }
        try {
            if (_json["id"].is_string()) {
                value = _json["id"].get<std::string>();
                return true;
            }
        } catch (...) {
            return false;
        }
        return false;
    }

    bool jsonrpc::has_id() {
        return _json.contains("id");
    }

    jsonrpc& jsonrpc::copy_id(jsonrpc& other) {
        if (_json.contains("id")) {
            if (_json["id"].is_number_integer()) {
                int64_t id = _json["id"].get<int64_t>();
                other.set_id(id);
            } else if (_json["id"].is_string()) {
                std::string id = _json["id"].get<std::string>();
                other.set_id(id);
            } else {
                other.set_id();
            }
        }
        return *this;
    }

    client* jsonrpc::get_client() {
        return _client;
    }

    nlohmann::json jsonrpc::compile() {
        return _json;
    }

    // === request 类实现 ===

    request::request(nlohmann::json& json, client* c) {
        _json = json;
        _client = c;
    }

    request& request::set_method(const std::string& value) {
        _json["method"] = value;
        return *this;
    }

    bool request::get_method(std::string& value) {
        if (!_json.contains("method")) {
            return false;
        }
        try {
            if (_json["method"].is_string()) {
                value = _json["method"].get<std::string>();
                return true;
            }
        } catch (...) {
            return false;
        }
        return false;
    }

    std::string request::get_method() {
        std::string value;
        if (get_method(value)) {
            return value;
        }
        return "";
    }

    request& request::clear_params() {
        if (_json.contains("params")) {
            _json.erase("params");
        }
        return *this;
    }

    request& request::set_params(nlohmann::json value) {
        _json["params"] = value;
        return *this;
    }

    bool request::get_params(nlohmann::json& value) {
        if (!_json.contains("params")) {
            value = nullptr;
            return false;
        }
        value = _json["params"];
        return true;
    }

    void request::validate() {
        // 检查 jsonrpc 字段
        if (!_json.contains("jsonrpc") || !_json["jsonrpc"].is_string() ||
            _json["jsonrpc"] != "2.0") {
            throw invalid_request_error("Missing or invalid 'jsonrpc' field. Must be '2.0'");
        }

        // 检查 method 字段
        if (!_json.contains("method") || !_json["method"].is_string()) {
            throw invalid_request_error("Missing or invalid 'method' field");
        }

    }

    // === response 类实现 ===

    response::response(nlohmann::json& json, client* c) {
        _json = json;
        _client = c;
    }

    response& response::set_result(nlohmann::json value) {
        // 清除错误字段
        if (_json.contains("error")) {
            _json.erase("error");
        }
        _json["result"] = value;
        return *this;
    }

    bool response::get_result(nlohmann::json& value) {
        if (!_json.contains("result")) {
            return false;
        }
        value = _json["result"];
        return true;
    }

    response& response::set_error(int64_t code, const std::string& message) {
        // 清除结果字段
        if (_json.contains("result")) {
            _json.erase("result");
        }

        nlohmann::json error_obj;
        error_obj["code"] = code;
        error_obj["message"] = message;
        _json["error"] = error_obj;

        return *this;
    }

    response& response::set_error(int64_t code, const std::string& message, nlohmann::json data) {
        // 清除结果字段
        if (_json.contains("result")) {
            _json.erase("result");
        }

        nlohmann::json error_obj;
        error_obj["code"] = code;
        error_obj["message"] = message;
        error_obj["data"] = data;
        _json["error"] = error_obj;

        return *this;
    }

    bool response::get_error_code(int64_t& value) {
        if (!_json.contains("error")) {
            return false;
        }
        try {
            auto& error_obj = _json["error"];
            if (error_obj.contains("code") && error_obj["code"].is_number_integer()) {
                value = error_obj["code"].get<int64_t>();
                return true;
            }
        } catch (...) {
            return false;
        }
        return false;
    }

    bool response::get_error_message(std::string& value) {
        if (!_json.contains("error")) {
            return false;
        }
        try {
            auto& error_obj = _json["error"];
            if (error_obj.contains("message") && error_obj["message"].is_string()) {
                value = error_obj["message"].get<std::string>();
                return true;
            }
        } catch (...) {
            return false;
        }
        return false;
    }

    bool response::get_error_data(nlohmann::json& value) {
        if (!_json.contains("error")) {
            return false;
        }
        try {
            auto& error_obj = _json["error"];
            if (error_obj.contains("data")) {
                value = error_obj["data"];
                return true;
            }
        } catch (...) {
            return false;
        }
        return false;
    }

    void response::validate() {
        // 检查 jsonrpc 字段
        if (!_json.contains("jsonrpc") || !_json["jsonrpc"].is_string() ||
            _json["jsonrpc"] != "2.0") {
            throw invalid_request_error("Missing or invalid 'jsonrpc' field. Must be '2.0'");
        }

        // 检查 result 或 error 字段（至少有一个）
        bool has_result = _json.contains("result");
        bool has_error = _json.contains("error");

        if (!has_result && !has_error) {
            throw invalid_request_error("Response must have either 'result' or 'error' field");
        }

        if (has_result && has_error) {
            throw invalid_request_error("Response cannot have both 'result' and 'error' fields");
        }
    }

} // namespace jsonrpc
} // namespace streamdock
