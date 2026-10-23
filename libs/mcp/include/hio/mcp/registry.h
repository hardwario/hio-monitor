#pragma once

// Registry - the tools, resources, resource templates and prompts of one MCP server, built once
// before the server starts (plan/features/0013 "Protocol core", ADR-0099, ADR-0107).
//
// Texts in specs are English (ADR-0103). std::string_view members must refer to static storage
// (string literals); QString/QJsonObject members are owned.
//
// ToolEnv is the per-server context every ToolSpec::start receives: the consent broker, the
// limits, the server's Offloader (blocking work on worker threads, ADR-0162) and an opaque
// `context` (std::any) set from ServerOptions::toolContext. hio_mcp never
// looks at the context. hio_mcp_tools may keep its IDeviceProvider there
// (`*env.contextAs<IDeviceProvider*>()`), or - simpler and fully typed - capture it in the start
// lambdas when it registers the tools.
//
// Resource and prompt handlers are synchronous and run on the owner thread (they read ConsoleLog).
// Thread safety: owner thread only.

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/mcp/consent.h"
#include "hio/mcp/limits.h"
#include "hio/mcp/offload.h"
#include "hio/mcp/request_context.h"
#include "hio/mcp/schema.h"
#include "hio/mcp/tool_call.h"

#include <QJsonObject>
#include <QString>

#include <any>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::mcp {

class ToolEnv final {
public:
    // `consentBroker`, `limits` and `offloader` must outlive the environment (the server owns
    // all three).
    ToolEnv(IConsentBroker& consentBroker, const Limits& limits, Offloader& offloader,
            std::any context)
        : m_consentBroker(&consentBroker), m_limits(&limits), m_offloader(&offloader),
          m_context(std::move(context)) {}

    [[nodiscard]] IConsentBroker& consentBroker() const noexcept { return *m_consentBroker; }
    [[nodiscard]] const Limits& limits() const noexcept { return *m_limits; }
    [[nodiscard]] Offloader& offloader() const noexcept { return *m_offloader; }
    [[nodiscard]] const std::any& context() const noexcept { return m_context; }
    // The context as T, or nullptr when it holds something else.
    template <class T> [[nodiscard]] const T* contextAs() const noexcept {
        return std::any_cast<T>(&m_context);
    }

private:
    IConsentBroker* m_consentBroker; // non-owning, never null
    const Limits* m_limits;          // non-owning, never null
    Offloader* m_offloader;          // non-owning, never null
    std::any m_context;
};

struct ToolAnnotations {
    bool readOnlyHint = false;
    bool destructiveHint = false;
    bool idempotentHint = false;
    bool openWorldHint = false;
};

// Creates the call for validated arguments (defaults filled in). Must not return nullptr (the
// server answers internal.unexpected); may return an already finished call.
using ToolStart = std::function<std::unique_ptr<ToolCall>(
    const RequestContext& context, const QJsonObject& arguments, ToolEnv& env)>;

// Optional argument checks the schema subset cannot express (plan/features/0013: "exactly one of
// path / catalog_id", a valid regular expression). Runs after validateArguments() on the completed
// arguments; a ValidationError (pointer with the "/arguments" prefix) becomes -32602 like a schema
// error, before the call is counted or started. (A hook beyond the plan's sketch, accepted in the
// review of plan 0013, Q3.)
using ToolValidate = std::function<std::optional<ValidationError>(const QJsonObject& arguments,
                                                                  const Limits& limits)>;

struct ToolSpec {
    std::string_view name;        // [A-Za-z0-9_.-]{1,128}
    std::string_view title;       // optional
    std::string_view description; // English (ADR-0103)
    QJsonObject inputSchema;      // schema.h subset; Schema::object() builds one
    QJsonObject outputSchema;     // any JSON Schema object; every v1 tool has one
    ToolAnnotations annotations;
    ConsentCategories consent; // static part; handlers can add Uicr at run time (consent.h)
    bool marker = false;       // ADR-0105: the handler writes console markers
    ToolStart start;
    ToolValidate validate; // optional
};

struct ResourceContent {
    QString uri;
    QString mimeType;
    QString text;
};

// The outcome of a resource read: contents, "not found" (-32002 legacy / -32602 modern) or an
// error (-32603 with data {"error": hio::toJson(error)}).
struct ResourceReadResult {
    enum class Status : std::uint8_t { Ok, NotFound, Failed };
    Status status = Status::Ok;
    std::vector<ResourceContent> contents;
    std::optional<hio::Error> error;

    [[nodiscard]] static ResourceReadResult ok(std::vector<ResourceContent> contents);
    [[nodiscard]] static ResourceReadResult text(const QString& uri, const QString& mimeType,
                                                 const QString& text);
    [[nodiscard]] static ResourceReadResult notFound();
    [[nodiscard]] static ResourceReadResult failed(hio::Error error);
};

// `variables`: the percent-decoded template variables ({id} -> "jlink:801000001"); empty for
// static resources.
using ResourceReader =
    std::function<ResourceReadResult(const RequestContext& context, const QString& uri,
                                     const std::map<QString, QString>& variables)>;

struct ResourceSpec {
    std::string_view uri; // e.g. "hio://devices"
    std::string_view name;
    std::string_view title;
    std::string_view description;
    std::string_view mimeType;
    ResourceReader read;
};

// RFC 6570 level 1: "{name}" matches one path segment ([^/]+).
struct ResourceTemplateSpec {
    std::string_view uriTemplate; // e.g. "hio://device/{id}/status"
    std::string_view name;
    std::string_view title;
    std::string_view description;
    std::string_view mimeType;
    ResourceReader read;
};

// A concrete resource listed by resources/list besides the static ones (open devices' URIs).
struct ResourceInfo {
    QString uri;
    QString name;
    QString title;
    QString description;
    QString mimeType;
};
using ResourceLister = std::function<std::vector<ResourceInfo>(const RequestContext& context)>;

struct PromptArgument {
    std::string_view name;
    std::string_view description;
    bool required = false;
};

struct PromptMessage {
    QString role; // "user" or "assistant"
    QString text;
};

struct PromptResult {
    QString description;
    std::vector<PromptMessage> messages;
};

// Required arguments are checked by the server (-32602) before the getter runs. An error becomes
// -32603 with data {"error": hio::toJson(error)}.
using PromptGetter = std::function<hio::Result<PromptResult>(
    const RequestContext& context, const std::map<QString, QString>& arguments)>;

struct PromptSpec {
    std::string_view name;
    std::string_view title;
    std::string_view description;
    std::vector<PromptArgument> arguments;
    PromptGetter get;
};

// A resolved resource URI: the reader and the template variables.
struct ResourceMatch {
    const ResourceReader* reader = nullptr; // non-owning, points into the registry
    std::map<QString, QString> variables;
};

class Registry {
public:
    // Errors (internal.unexpected, detail = reason): duplicate name/URI, invalid name, missing
    // handler, input schema outside the schema.h subset, malformed template.
    [[nodiscard]] Status addTool(ToolSpec spec);
    [[nodiscard]] Status addResource(ResourceSpec spec);
    [[nodiscard]] Status addResourceTemplate(ResourceTemplateSpec spec);
    [[nodiscard]] Status addPrompt(PromptSpec spec);
    void setResourceLister(ResourceLister lister) { m_lister = std::move(lister); }

    [[nodiscard]] std::span<const ToolSpec> tools() const noexcept { return m_tools; }
    [[nodiscard]] std::span<const ResourceSpec> resources() const noexcept { return m_resources; }
    [[nodiscard]] std::span<const ResourceTemplateSpec> resourceTemplates() const noexcept {
        return m_templates;
    }
    [[nodiscard]] std::span<const PromptSpec> prompts() const noexcept { return m_prompts; }
    [[nodiscard]] const ResourceLister& resourceLister() const noexcept { return m_lister; }

    // Non-owning lookups (nullptr / nullopt when unknown); valid while the registry lives.
    [[nodiscard]] const ToolSpec* findTool(std::string_view name) const;
    [[nodiscard]] const PromptSpec* findPrompt(std::string_view name) const;
    // Static resources first, then the templates in registration order.
    [[nodiscard]] std::optional<ResourceMatch> matchResource(const QString& uri) const;

private:
    std::vector<ToolSpec> m_tools;
    std::vector<ResourceSpec> m_resources;
    std::vector<ResourceTemplateSpec> m_templates;
    std::vector<PromptSpec> m_prompts;
    ResourceLister m_lister;
};

// Template matching (exposed for tests): the variables, or nullopt when `uri` does not match.
[[nodiscard]] std::optional<std::map<QString, QString>>
matchUriTemplate(std::string_view uriTemplate, const QString& uri);

} // namespace hio::mcp
