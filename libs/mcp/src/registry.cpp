#include "hio/mcp/registry.h"

#include "hio/core/error.h"
#include "hio/core/result.h"
#include "hio/mcp/schema.h"

#include <QByteArray>
#include <QLatin1StringView>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <QtCore/qcborcommon.h>
#include <QtTypes>

#include <algorithm>
#include <cstddef>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hio::mcp {

namespace {

std::unexpected<Error> registryError(std::string_view what, std::string_view name) {
    std::string detail(what);
    detail += ": ";
    detail += name;
    return std::unexpected(makeError(ErrorCode::InternalUnexpected, std::move(detail)));
}

bool validName(std::string_view name) {
    if (name.empty() || name.size() > 128) {
        return false;
    }
    return std::ranges::all_of(name, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
               c == '_' || c == '-' || c == '.';
    });
}

QString latin1(std::string_view text) {
    return QLatin1StringView(text.data(), static_cast<qsizetype>(text.size())).toString();
}

// "{name}" segments of a level-1 URI template; nullopt when malformed.
std::optional<std::vector<QString>> templateVariables(std::string_view uriTemplate) {
    std::vector<QString> names;
    std::size_t pos = 0;
    while (pos < uriTemplate.size()) {
        const std::size_t open = uriTemplate.find('{', pos);
        const std::size_t close = uriTemplate.find('}', pos);
        if (open == std::string_view::npos) {
            if (close != std::string_view::npos) {
                return std::nullopt;
            }
            break;
        }
        if (close == std::string_view::npos || close < open + 2) {
            return std::nullopt;
        }
        const std::string_view name = uriTemplate.substr(open + 1, close - open - 1);
        if (!validName(name) || name.find('{') != std::string_view::npos) {
            return std::nullopt;
        }
        names.push_back(latin1(name));
        pos = close + 1;
    }
    return names;
}

} // namespace

ResourceReadResult ResourceReadResult::ok(std::vector<ResourceContent> contents) {
    return ResourceReadResult{.status = Status::Ok, .contents = std::move(contents), .error = {}};
}

ResourceReadResult ResourceReadResult::text(const QString& uri, const QString& mimeType,
                                            const QString& text) {
    return ok({ResourceContent{.uri = uri, .mimeType = mimeType, .text = text}});
}

ResourceReadResult ResourceReadResult::notFound() {
    return ResourceReadResult{.status = Status::NotFound, .contents = {}, .error = {}};
}

ResourceReadResult ResourceReadResult::failed(Error error) {
    return ResourceReadResult{.status = Status::Failed, .contents = {}, .error = std::move(error)};
}

std::optional<std::map<QString, QString>> matchUriTemplate(std::string_view uriTemplate,
                                                           const QString& uri) {
    const auto names = templateVariables(uriTemplate);
    if (!names) {
        return std::nullopt;
    }
    QString pattern = QStringLiteral("^");
    std::size_t pos = 0;
    while (pos < uriTemplate.size()) {
        const std::size_t open = uriTemplate.find('{', pos);
        if (open == std::string_view::npos) {
            pattern += QRegularExpression::escape(latin1(uriTemplate.substr(pos)));
            break;
        }
        pattern += QRegularExpression::escape(latin1(uriTemplate.substr(pos, open - pos)));
        pattern += QStringLiteral("([^/]+)");
        pos = uriTemplate.find('}', open) + 1;
    }
    pattern += QLatin1Char('$');
    const QRegularExpression regex(pattern);
    const QRegularExpressionMatch match = regex.match(uri);
    if (!match.hasMatch()) {
        return std::nullopt;
    }
    std::map<QString, QString> variables;
    for (std::size_t i = 0; i < names->size(); ++i) {
        variables.emplace(names->at(i), QUrl::fromPercentEncoding(
                                            match.captured(static_cast<int>(i) + 1).toUtf8()));
    }
    return variables;
}

Status Registry::addTool(ToolSpec spec) {
    if (!validName(spec.name)) {
        return registryError("invalid tool name", spec.name);
    }
    if (findTool(spec.name) != nullptr) {
        return registryError("duplicate tool", spec.name);
    }
    if (!spec.start) {
        return registryError("tool without start function", spec.name);
    }
    if (auto checked = checkSchemaSubset(spec.inputSchema); !checked) {
        return registryError("unsupported input schema (" + checked.error().toStdString() + ")",
                             spec.name);
    }
    m_tools.push_back(std::move(spec));
    return {};
}

Status Registry::addResource(ResourceSpec spec) {
    if (spec.uri.empty() || !spec.read) {
        return registryError("invalid resource", spec.uri);
    }
    const bool duplicate = std::ranges::any_of(
        m_resources, [&spec](const ResourceSpec& other) { return other.uri == spec.uri; });
    if (duplicate) {
        return registryError("duplicate resource", spec.uri);
    }
    m_resources.push_back(std::move(spec));
    return {};
}

Status Registry::addResourceTemplate(ResourceTemplateSpec spec) {
    const auto names = templateVariables(spec.uriTemplate);
    if (!names || names->empty() || !spec.read) {
        return registryError("invalid resource template", spec.uriTemplate);
    }
    const bool duplicate =
        std::ranges::any_of(m_templates, [&spec](const ResourceTemplateSpec& other) {
            return other.uriTemplate == spec.uriTemplate;
        });
    if (duplicate) {
        return registryError("duplicate resource template", spec.uriTemplate);
    }
    m_templates.push_back(std::move(spec));
    return {};
}

Status Registry::addPrompt(PromptSpec spec) {
    if (!validName(spec.name) || !spec.get) {
        return registryError("invalid prompt", spec.name);
    }
    if (findPrompt(spec.name) != nullptr) {
        return registryError("duplicate prompt", spec.name);
    }
    m_prompts.push_back(std::move(spec));
    return {};
}

const ToolSpec* Registry::findTool(std::string_view name) const {
    const auto it =
        std::ranges::find_if(m_tools, [name](const ToolSpec& spec) { return spec.name == name; });
    return it == m_tools.end() ? nullptr : &*it;
}

const PromptSpec* Registry::findPrompt(std::string_view name) const {
    const auto it = std::ranges::find_if(
        m_prompts, [name](const PromptSpec& spec) { return spec.name == name; });
    return it == m_prompts.end() ? nullptr : &*it;
}

std::optional<ResourceMatch> Registry::matchResource(const QString& uri) const {
    for (const ResourceSpec& spec : m_resources) {
        if (latin1(spec.uri) == uri) {
            return ResourceMatch{.reader = &spec.read, .variables = {}};
        }
    }
    for (const ResourceTemplateSpec& spec : m_templates) {
        if (auto variables = matchUriTemplate(spec.uriTemplate, uri)) {
            return ResourceMatch{.reader = &spec.read, .variables = std::move(*variables)};
        }
    }
    return std::nullopt;
}

} // namespace hio::mcp
