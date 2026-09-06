/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore/Audacity CLA applies
 *
 * Copyright (C) 2026 MuseScore/Audacity and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "mcpcontroller.h"

#include <sstream>
#include <fstream>
#include <iomanip>
#include <random>

#include "mcpserver.h"

#include "global/stringutils.h"
#include "global/serialization/json.h"

#include "log.h"
#include "thirdparty/kors_logger/src/log_base.h"

using namespace muse::rcontrol::mcp;
using namespace muse::rcommand;

McpController::McpController(const modularity::ContextPtr& iocCtx)
    : Contextable(iocCtx)
{
}

McpController::~McpController()
{
    deinit();
}

//! NOTE The tool name must be in the format: group_name
static std::string commandToToolName(const Command& command)
{
    std::string path = command.path();
    muse::strings::replace(path, "/", "_");
    return path;
}

static muse::rcontrol::mcp::DataType toMcpDataType(muse::rcommand::DataType type)
{
    using McpDataType = muse::rcontrol::mcp::DataType;
    switch (type) {
    case muse::rcommand::DataType::String: return McpDataType::String;
    case muse::rcommand::DataType::Integer: return McpDataType::Integer;
    case muse::rcommand::DataType::Float: return McpDataType::Float;
    case muse::rcommand::DataType::Boolean: return McpDataType::Boolean;
    case muse::rcommand::DataType::Object: return McpDataType::Object;
    case muse::rcommand::DataType::Array: return McpDataType::Array;
    case muse::rcommand::DataType::Null: return McpDataType::Null;
    case muse::rcommand::DataType::Undefined:
    default: return McpDataType::Undefined;
    }
}

static muse::JsonValue valToJsonValue(const muse::Val& val)
{
    if (val.isNull()) {
        return muse::JsonValue();
    }

    switch (val.type()) {
    case muse::Val::Type::Bool: return muse::JsonValue(val.toBool());
    case muse::Val::Type::Int:
    case muse::Val::Type::Int64: return muse::JsonValue(val.toInt());
    case muse::Val::Type::Double: return muse::JsonValue(val.toDouble());
    default: return muse::JsonValue(val.toString());
    }
}

//! NOTE JsonValue::toStdString() only returns a real value for a JSON string -
//! for every other JSON type (number, bool, etc.) it silently returns an EMPTY
//! string. Found via a crash dump: a numeric MCP argument (e.g. select-time's
//! start/end) was reaching handlers as "" instead of "0"/"2.5", and an unguarded
//! std::stod("") downstream threw an uncaught std::invalid_argument that crashed
//! the whole app. Convert every JSON value type to its string form explicitly
//! here instead of relying on toStdString() for non-string types.
static std::string jsonValueToString(const muse::JsonValue& value)
{
    if (value.isString()) {
        return value.toStdString();
    }
    if (value.isNumber()) {
        std::ostringstream oss;
        oss.precision(15);
        oss << value.toDouble();
        return oss.str();
    }
    if (value.isBool()) {
        return value.toBool() ? "true" : "false";
    }
    return value.toStdString();
}

static CommandQuery commandQuery(const std::string& name, const muse::JsonObject& args)
{
    std::string path = name;
    muse::strings::replace(path, "_", "/");
    Command cmd(std::string(COMMAND_SCHEME), path);
    CommandQuery q(cmd);

    //! NOTE args.keys() asserts internally (picojson's get<T> type-check) if the
    //! underlying JSON value was never actually initialized as an object - which is
    //! the case for a default-constructed/invalid JsonObject. Confirmed via a crash
    //! dump: this happened even for a syntactically valid empty "{}" arguments value
    //! in some call, so isValid() is checked defensively rather than assumed.
    if (args.isValid()) {
        for (const std::string& key : args.keys()) {
            q.set(key, jsonValueToString(args.value(key)));
        }
    }

    return q;
}


//! The token is written where only this user can read it, and MCP clients read it
//! from the same place - so a client needs no configuration, while a web page (which
//! cannot read local files) and another user's process cannot obtain it.
std::string McpController::resolveAuthToken() const
{
    const muse::io::path_t dir = globalConfiguration()->userAppDataPath();
    const std::string tokenPath = (dir.toStdString() + "/mcp_token");

    {
        std::ifstream in(tokenPath);
        std::string existing;
        if (in && std::getline(in, existing)) {
            muse::strings::trim(existing);
            if (existing.size() >= 32) {
                return existing;
            }
        }
    }

    std::random_device rd;
    std::ostringstream oss;
    for (int i = 0; i < 8; ++i) {
        oss << std::hex << std::setw(8) << std::setfill('0') << rd();
    }
    const std::string token = oss.str();

    std::ofstream out(tokenPath, std::ios::trunc);
    if (!out) {
        LOGE() << "could not write the MCP token to " << tokenPath
               << " - the bridge will refuse every request until this succeeds";
        return {};
    }
    out << token << std::endl;
    LOGI() << "created a new MCP auth token at " << tokenPath;
    return token;
}

void McpController::init()
{
    m_mcpServer = std::make_unique<McpServer>(application()->version().toStdString());
    m_mcpServer->setAuthToken(resolveAuthToken());

    m_mcpServer->onToolsListRequest([this](const McpServer::ToolsListResultHandler& onResult) {
        std::vector<Tool> tools = makeToolsList();
        onResult(tools);
    });

    m_mcpServer->onToolsCallRequest([this](const std::string& name,
                                           const muse::JsonObject& args,
                                           const McpServer::ToolsCallResultHandler& onResult)
    {
        LOGDA() << "Tools call: " << name;

        commandsDispatcher()->dispatch(commandQuery(name, args))
        .onResolve(this, [onResult](const Response& response) {
            ToolResult result;
            result.isError = !response.ret.success();

            if (!response.ret.text().empty()) {
                result.content.push_back(response.ret.text());
            }
            if (response.data.has_value()) {
                if (const std::string* text = std::any_cast<std::string>(&response.data)) {
                    result.content.push_back(*text);
                }
            }

            onResult(result);
        });
    });

    m_mcpServer->init();
}

void McpController::deinit()
{
    if (m_mcpServer) {
        m_mcpServer->deinit();
        m_mcpServer = nullptr;
    }
}

std::vector<Tool> McpController::makeToolsList() const
{
    std::vector<Tool> tools;
    auto commandList = commandsRegister()->commandInfoList();
    tools.reserve(commandList.size());
    for (const auto& info : commandList) {
        Tool tool;
        tool.name = commandToToolName(info.command);
        tool.title = info.title.raw().translated().toStdString();
        tool.description = info.description.translated().toStdString();
        InputSchema schema;
        for (const auto& [argName, arg] : info.inputSchema.args) {
            Property property;
            property.type = toMcpDataType(arg.type);
            property.description = arg.description.toStdString();
            property.minimum = valToJsonValue(arg.min);
            property.maximum = valToJsonValue(arg.max);
            schema.properties[argName] = property;
        }
        tool.inputSchema = schema;
        tools.push_back(std::move(tool));
    }
    return tools;
}
