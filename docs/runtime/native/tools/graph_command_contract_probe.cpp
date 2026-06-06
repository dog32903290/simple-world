#include "../graph/GraphCommandDispatcher.hpp"
#include "../graph/GraphValidator.hpp"
#include "../nodes/NodeSpecRegistry.hpp"
#include "../runtime/RuntimeGraphBuilder.hpp"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using simple_world::graph::Diagnostics;
using simple_world::graph::GraphCommand;
using simple_world::graph::GraphDocument;
using simple_world::graph::GraphState;
using simple_world::graph::ParamValue;
using simple_world::graph::Position;
using simple_world::graph::PortRef;

struct JsonValue {
    using Array = std::vector<JsonValue>;
    using Object = std::map<std::string, JsonValue>;
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    Storage value = nullptr;

    const Object& object() const {
        if (!std::holds_alternative<Object>(value)) {
            throw std::runtime_error("expected object");
        }
        return std::get<Object>(value);
    }

    const Array& array() const {
        if (!std::holds_alternative<Array>(value)) {
            throw std::runtime_error("expected array");
        }
        return std::get<Array>(value);
    }

    const std::string& string() const {
        if (!std::holds_alternative<std::string>(value)) {
            throw std::runtime_error("expected string");
        }
        return std::get<std::string>(value);
    }

    double number() const {
        if (!std::holds_alternative<double>(value)) {
            throw std::runtime_error("expected number");
        }
        return std::get<double>(value);
    }
};

class FixtureJsonParser {
public:
    explicit FixtureJsonParser(std::string source) : source(std::move(source)) {}

    JsonValue parse() {
        auto value = parseValue();
        skipWhitespace();
        if (index != source.size()) {
            throw std::runtime_error("unexpected trailing JSON input");
        }
        return value;
    }

private:
    JsonValue parseValue() {
        skipWhitespace();
        if (index >= source.size()) {
            throw std::runtime_error("unexpected end of JSON input");
        }
        const char ch = source[index];
        if (ch == '{') {
            return JsonValue{ parseObject() };
        }
        if (ch == '[') {
            return JsonValue{ parseArray() };
        }
        if (ch == '"') {
            return JsonValue{ parseString() };
        }
        if (ch == '-' || std::isdigit(static_cast<unsigned char>(ch))) {
            return JsonValue{ parseNumber() };
        }
        if (consumeLiteral("true")) {
            return JsonValue{ true };
        }
        if (consumeLiteral("false")) {
            return JsonValue{ false };
        }
        if (consumeLiteral("null")) {
            return JsonValue{ nullptr };
        }
        throw std::runtime_error("unsupported JSON value");
    }

    JsonValue::Object parseObject() {
        expect('{');
        JsonValue::Object object;
        skipWhitespace();
        if (consume('}')) {
            return object;
        }
        while (true) {
            const auto key = parseString();
            skipWhitespace();
            expect(':');
            object[key] = parseValue();
            skipWhitespace();
            if (consume('}')) {
                return object;
            }
            expect(',');
        }
    }

    JsonValue::Array parseArray() {
        expect('[');
        JsonValue::Array array;
        skipWhitespace();
        if (consume(']')) {
            return array;
        }
        while (true) {
            array.push_back(parseValue());
            skipWhitespace();
            if (consume(']')) {
                return array;
            }
            expect(',');
        }
    }

    std::string parseString() {
        expect('"');
        std::string result;
        while (index < source.size()) {
            const char ch = source[index++];
            if (ch == '"') {
                return result;
            }
            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }
            if (index >= source.size()) {
                throw std::runtime_error("unterminated JSON escape");
            }
            const char escaped = source[index++];
            switch (escaped) {
                case '"':
                case '\\':
                case '/':
                    result.push_back(escaped);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                default:
                    throw std::runtime_error("unsupported JSON string escape");
            }
        }
        throw std::runtime_error("unterminated JSON string");
    }

    double parseNumber() {
        const auto start = index;
        if (consume('-')) {
        }
        while (index < source.size() && std::isdigit(static_cast<unsigned char>(source[index]))) {
            ++index;
        }
        if (consume('.')) {
            while (index < source.size() && std::isdigit(static_cast<unsigned char>(source[index]))) {
                ++index;
            }
        }
        if (index < source.size() && (source[index] == 'e' || source[index] == 'E')) {
            ++index;
            if (index < source.size() && (source[index] == '+' || source[index] == '-')) {
                ++index;
            }
            while (index < source.size() && std::isdigit(static_cast<unsigned char>(source[index]))) {
                ++index;
            }
        }
        return std::stod(source.substr(start, index - start));
    }

    bool consumeLiteral(const std::string& literal) {
        if (source.compare(index, literal.size(), literal) != 0) {
            return false;
        }
        index += literal.size();
        return true;
    }

    bool consume(char expected) {
        skipWhitespace();
        if (index < source.size() && source[index] == expected) {
            ++index;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        skipWhitespace();
        if (index >= source.size() || source[index] != expected) {
            throw std::runtime_error(std::string("expected JSON character ") + expected);
        }
        ++index;
    }

    void skipWhitespace() {
        while (index < source.size() && std::isspace(static_cast<unsigned char>(source[index]))) {
            ++index;
        }
    }

    std::string source;
    std::size_t index = 0;
};

struct FixtureCommandEntry {
    std::string source;
    GraphCommand command;
};

struct Fixture {
    std::string graphId;
    std::vector<FixtureCommandEntry> commands;
};

std::string readFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("failed to open file: " + path);
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

const JsonValue& requiredField(const JsonValue::Object& object, const std::string& key) {
    const auto found = object.find(key);
    if (found == object.end()) {
        throw std::runtime_error("missing fixture field: " + key);
    }
    return found->second;
}

std::optional<const JsonValue*> optionalField(const JsonValue::Object& object, const std::string& key) {
    const auto found = object.find(key);
    if (found == object.end()) {
        return std::nullopt;
    }
    return &found->second;
}

std::string optionalString(const JsonValue::Object& object, const std::string& key, const std::string& fallback) {
    const auto field = optionalField(object, key);
    if (!field.has_value()) {
        return fallback;
    }
    return (*field)->string();
}

Position parsePosition(const JsonValue::Object& command) {
    Position position;
    const auto field = optionalField(command, "position");
    if (!field.has_value()) {
        return position;
    }
    const auto& object = (*field)->object();
    position.x = requiredField(object, "x").number();
    position.y = requiredField(object, "y").number();
    return position;
}

PortRef parsePortRef(const JsonValue& value) {
    const auto& object = value.object();
    return PortRef{
        requiredField(object, "nodeId").string(),
        requiredField(object, "port").string(),
    };
}

ParamValue parseParamValue(const JsonValue& value) {
    if (std::holds_alternative<double>(value.value)) {
        return value.number();
    }
    if (std::holds_alternative<bool>(value.value)) {
        return std::get<bool>(value.value);
    }
    if (std::holds_alternative<std::string>(value.value)) {
        return value.string();
    }
    throw std::runtime_error("unsupported fixture parameter value");
}

GraphCommand parseCommand(const JsonValue& value) {
    const auto& object = value.object();
    const auto type = requiredField(object, "type").string();
    if (type == "CreateNode") {
        return simple_world::graph::CreateNodeCommand{
            requiredField(object, "nodeId").string(),
            requiredField(object, "nodeType").string(),
            parsePosition(object),
        };
    }
    if (type == "SelectNode") {
        return simple_world::graph::SelectNodeCommand{
            requiredField(object, "nodeId").string(),
            optionalString(object, "mode", "replace"),
        };
    }
    if (type == "MoveNode") {
        return simple_world::graph::MoveNodeCommand{
            requiredField(object, "nodeId").string(),
            parsePosition(object),
        };
    }
    if (type == "BeginCableDrag") {
        return simple_world::graph::BeginCableDragCommand{
            parsePortRef(requiredField(object, "from")),
        };
    }
    if (type == "HoverPort") {
        return simple_world::graph::HoverPortCommand{
            parsePortRef(requiredField(object, "port")),
        };
    }
    if (type == "CommitCableDrag") {
        return simple_world::graph::CommitCableDragCommand{
            parsePortRef(requiredField(object, "to")),
        };
    }
    if (type == "CancelCableDrag") {
        return simple_world::graph::CancelCableDragCommand{};
    }
    if (type == "DeleteSelection") {
        return simple_world::graph::DeleteSelectionCommand{};
    }
    if (type == "SetParameter") {
        return simple_world::graph::SetParameterCommand{
            requiredField(object, "nodeId").string(),
            requiredField(object, "param").string(),
            parseParamValue(requiredField(object, "value")),
        };
    }
    throw std::runtime_error("unsupported fixture command type: " + type);
}

Fixture parseFixture(const std::string& fixturePath) {
    // This parser is intentionally bounded to the Task 1 contract fixtures:
    // a top-level object with graphId and command entries using scalar fields,
    // port refs, and position objects. It is not a general JSON API for the app.
    const auto root = FixtureJsonParser(readFile(fixturePath)).parse();
    const auto& object = root.object();
    Fixture fixture;
    fixture.graphId = requiredField(object, "graphId").string();
    for (const auto& entryValue : requiredField(object, "commands").array()) {
        const auto& entry = entryValue.object();
        fixture.commands.push_back(FixtureCommandEntry{
            optionalString(entry, "source", "fixture"),
            parseCommand(requiredField(entry, "command")),
        });
    }
    return fixture;
}

std::string escapeJson(const std::string& value) {
    std::ostringstream out;
    for (const char ch : value) {
        switch (ch) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                out << ch;
                break;
        }
    }
    return out.str();
}

void writeString(std::ostream& out, const std::string& value) {
    out << "\"" << escapeJson(value) << "\"";
}

void writeParamValue(std::ostream& out, const ParamValue& value) {
    std::visit([&](const auto& typedValue) {
        using ValueType = std::decay_t<decltype(typedValue)>;
        if constexpr (std::is_same_v<ValueType, double>) {
            out << std::setprecision(15) << typedValue;
        } else if constexpr (std::is_same_v<ValueType, bool>) {
            out << (typedValue ? "true" : "false");
        } else if constexpr (std::is_same_v<ValueType, std::string>) {
            writeString(out, typedValue);
        } else if constexpr (std::is_same_v<ValueType, simple_world::graph::NumericArray>) {
            out << "[";
            for (std::size_t index = 0; index < typedValue.size(); ++index) {
                if (index > 0) {
                    out << ",";
                }
                out << std::setprecision(15) << typedValue[index];
            }
            out << "]";
        } else {
            out << "{";
            bool first = true;
            for (const auto& entry : typedValue) {
                if (!first) {
                    out << ",";
                }
                first = false;
                writeString(out, entry.first);
                out << ":" << std::setprecision(15) << entry.second;
            }
            out << "}";
        }
    }, value);
}

void writePortRef(std::ostream& out, const PortRef& port) {
    out << "{\"nodeId\":";
    writeString(out, port.nodeId);
    out << ",\"port\":";
    writeString(out, port.port);
    out << "}";
}

void writeDiagnostics(std::ostream& out, const Diagnostics& diagnostics) {
    out << "[";
    for (std::size_t index = 0; index < diagnostics.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        const auto& diagnostic = diagnostics[index];
        out << "{\"code\":";
        writeString(out, diagnostic.code);
        out << ",\"message\":";
        writeString(out, diagnostic.message);
        out << ",\"severity\":";
        writeString(out, diagnostic.severity);
        out << "}";
    }
    out << "]";
}

void writeGraphDocument(std::ostream& out, const GraphDocument& document) {
    out << "{\"kind\":";
    writeString(out, document.kind);
    out << ",\"version\":";
    writeString(out, document.version);
    out << ",\"graphId\":";
    writeString(out, document.graphId);
    out << ",\"nodes\":[";
    for (std::size_t nodeIndex = 0; nodeIndex < document.nodes.size(); ++nodeIndex) {
        if (nodeIndex > 0) {
            out << ",";
        }
        const auto& node = document.nodes[nodeIndex];
        out << "{\"id\":";
        writeString(out, node.id);
        out << ",\"type\":";
        writeString(out, node.type);
        out << ",\"position\":{\"x\":" << std::setprecision(15) << node.position.x
            << ",\"y\":" << std::setprecision(15) << node.position.y << "}";
        out << ",\"params\":{";
        bool firstParam = true;
        for (const auto& param : node.params) {
            if (!firstParam) {
                out << ",";
            }
            firstParam = false;
            writeString(out, param.first);
            out << ":";
            writeParamValue(out, param.second);
        }
        out << "}}";
    }
    out << "],\"edges\":[";
    for (std::size_t edgeIndex = 0; edgeIndex < document.edges.size(); ++edgeIndex) {
        if (edgeIndex > 0) {
            out << ",";
        }
        const auto& edge = document.edges[edgeIndex];
        out << "{\"id\":";
        writeString(out, edge.id);
        out << ",\"from\":";
        writePortRef(out, edge.from);
        out << ",\"to\":";
        writePortRef(out, edge.to);
        out << ",\"type\":";
        writeString(out, edge.type);
        out << "}";
    }
    out << "]}";
}

void writeRuntimeGraph(std::ostream& out, const simple_world::runtime::RuntimeGraph& runtimeGraph) {
    out << "{\"kind\":\"RuntimeGraph\",\"graphId\":";
    writeString(out, runtimeGraph.graphId);
    out << ",\"cookOrder\":[";
    for (std::size_t index = 0; index < runtimeGraph.cookOrder.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        writeString(out, runtimeGraph.cookOrder[index]);
    }
    out << "],\"nodes\":[";
    for (std::size_t index = 0; index < runtimeGraph.nodes.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        const auto& node = runtimeGraph.nodes[index];
        out << "{\"id\":";
        writeString(out, node.id);
        out << ",\"type\":";
        writeString(out, node.type);
        out << ",\"domain\":\"frame\"}";
    }
    out << "],\"edges\":[";
    for (std::size_t index = 0; index < runtimeGraph.edges.size(); ++index) {
        if (index > 0) {
            out << ",";
        }
        const auto& edge = runtimeGraph.edges[index];
        out << "{\"from\":";
        writePortRef(out, edge.from);
        out << ",\"to\":";
        writePortRef(out, edge.to);
        out << ",\"type\":";
        writeString(out, edge.type);
        out << "}";
    }
    out << "]}";
}

void writeResult(
    std::ostream& out,
    const std::string& graphId,
    const std::string& status,
    bool cppCommandDispatcher,
    bool runtimeDirty,
    const Diagnostics& diagnostics
) {
    out << "{\"kind\":\"CppGraphCommandContractResult\",\"graphId\":";
    writeString(out, graphId);
    out << ",\"status\":";
    writeString(out, status);
    out << ",\"claims\":{\"cppCommandDispatcher\":"
        << (cppCommandDispatcher ? "true" : "false")
        << ",\"runtimeDirty\":" << (runtimeDirty ? "true" : "false") << "}";
    out << ",\"diagnostics\":";
    writeDiagnostics(out, diagnostics);
    out << "}";
}

void writeJsonFile(const std::string& path, const std::function<void(std::ostream&)>& writer) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("failed to write file: " + path);
    }
    writer(file);
    file << "\n";
}

Diagnostics appendDiagnostics(Diagnostics base, const Diagnostics& extra) {
    base.insert(base.end(), extra.begin(), extra.end());
    return base;
}

std::string joinPath(const std::string& directory, const std::string& name) {
    if (!directory.empty() && directory.back() == '/') {
        return directory + name;
    }
    return directory + "/" + name;
}

int runProbe(const std::string& fixturePath, const std::string& outDir) {
    const auto fixture = parseFixture(fixturePath);
    const auto registry = simple_world::nodes::NodeSpecRegistry::createSeedRegistry();
    GraphState state = simple_world::graph::createInitialGraphState(fixture.graphId);
    Diagnostics diagnostics;

    for (const auto& entry : fixture.commands) {
        const auto result = simple_world::graph::dispatchGraphCommand(state, entry.command, registry);
        state = result.state;
        diagnostics = appendDiagnostics(std::move(diagnostics), result.diagnostics);
    }

    const auto validationDiagnostics = simple_world::graph::validateGraphState(state, registry);
    diagnostics = appendDiagnostics(std::move(diagnostics), validationDiagnostics);
    const auto runtimeBuild = simple_world::runtime::buildRuntimeGraph(state, registry);
    diagnostics = appendDiagnostics(std::move(diagnostics), runtimeBuild.diagnostics);

    const GraphDocument document = simple_world::graph::serializeGraphDocument(state);
    const GraphState reloadedState = simple_world::graph::deserializeGraphDocument(document);
    const GraphDocument reloadedDocument = simple_world::graph::serializeGraphDocument(reloadedState);
    const std::string status = diagnostics.empty() ? "passed" : "diagnosed";

    writeJsonFile(joinPath(outDir, "cpp_graph_command_contract_result.json"), [&](std::ostream& out) {
        writeResult(out, fixture.graphId, status, true, state.runtimeDirty, diagnostics);
    });
    writeJsonFile(joinPath(outDir, "graph_document.json"), [&](std::ostream& out) {
        writeGraphDocument(out, document);
    });
    writeJsonFile(joinPath(outDir, "runtime_graph.json"), [&](std::ostream& out) {
        writeRuntimeGraph(out, runtimeBuild.runtimeGraph);
    });
    writeJsonFile(joinPath(outDir, "diagnostics.json"), [&](std::ostream& out) {
        writeDiagnostics(out, diagnostics);
    });
    writeJsonFile(joinPath(outDir, "reloaded_graph_document.json"), [&](std::ostream& out) {
        writeGraphDocument(out, reloadedDocument);
    });
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: graph_command_contract_probe <fixture> <out_dir>\n";
        return 2;
    }

    try {
        return runProbe(argv[1], argv[2]);
    } catch (const std::exception& exc) {
        std::cerr << exc.what() << "\n";
        return 1;
    }
}
