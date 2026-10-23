#include "stdin_line_source.h"

#include "hio/mcp/stdio_transport.h"
#include "terminal.h"

#include <QByteArray>

#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace hio::cli {

StdinLineSource::StdinLineSource(std::size_t maxLineBytes, std::size_t maxQueuedLines,
                                 std::size_t maxQueuedBytes, StdinLineReader::ReadFunction read)
    : m_reader(std::make_unique<StdinLineReader>(
          StdinReaderOptions{.maxLineBytes = maxLineBytes,
                             .maxQueuedLines = maxQueuedLines,
                             .maxQueuedBytes = maxQueuedBytes,
                             .overlong = OverlongLinePolicy::DropAndReport,
                             .notify = [this] { wake(); }},
          std::move(read))) {}

StdinLineSource::~StdinLineSource() {
    m_reader.reset(); // joins the reader thread before the wake-up state goes away
}

std::optional<hio::mcp::ILineSource::Line> StdinLineSource::takeLine() {
    if (m_end) {
        return std::nullopt;
    }
    std::optional<StdinItem> item = m_reader->take();
    if (!item) {
        return std::nullopt;
    }
    switch (item->kind) {
    case StdinItem::Kind::Line:
        return Line{.text = QByteArray::fromStdString(item->text), .overlong = false};
    case StdinItem::Kind::Overlong:
        return Line{.text = {}, .overlong = true};
    case StdinItem::Kind::End:
        break;
    }
    m_end = true;
    return std::nullopt;
}

void StdinLineSource::setWakeup(std::function<void()> wakeup) {
    const std::scoped_lock lock(m_mutex);
    m_wakeup = std::move(wakeup);
}

void StdinLineSource::wake() {
    const std::scoped_lock lock(m_mutex);
    if (m_wakeup) {
        m_wakeup();
    }
}

} // namespace hio::cli
