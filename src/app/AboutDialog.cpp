#include "AboutDialog.h"

#include "Theme.h"

#include "SkiaCanvas.h"

#include <algorithm>
#include <cmath>

namespace uci_host::app
{

namespace
{

using reckless::gfx::Justify;
using reckless::gfx::RectF;
using reckless::gfx::RectI;

constexpr int   kMarkSide     = 112;    ///< The mark's square.
constexpr int   kTextLeft     = 136;    ///< The text beside the mark, from the content's left.
constexpr int   kLine         = 24;     ///< A line of body text.
constexpr int   kNameHeight   = 38;     ///< The app's name.
constexpr int   kCopyrightTop = 68;     ///< The copyright's line, from the mark's top: under the name and the version.
constexpr int   kTermsGap     = 18;     ///< Between the mark and the terms under it.
constexpr int   kSectionGap   = 16;     ///< Above the engines' section, and above the notices.
constexpr int   kLabelHeight  = 28;     ///< The engines' section's label.
constexpr int   kNoticeHeight = 24;     ///< The notices' note: a line.
constexpr float kLicenceLeft  = 240.0f; ///< The engines' licences, from the content's left.
constexpr int   kCardWidth    = 640;    ///< The card's width.
constexpr int   kTitleBand    = 48;     ///< The card's title band (DialogFrame's).
constexpr int   kFooterBand   = 58;     ///< The card's button band (DialogFrame's).

constexpr const char *kName       = "Reckless UCI Host";
constexpr const char *kCopyright  = "Copyright 2026 Jonny Reckless. Reckless Technology.";
constexpr const char *kTerms      = "Free, proprietary software, used under its end-user licence (EULA.txt).";
constexpr const char *kEngineNote = "Each engine is a separate program the host runs over UCI, under its own licence.";
constexpr const char *kNotices =
    "Third-party notices, the engines' among them, are in THIRD-PARTY-NOTICES.txt, installed beside the app.";

/** @return The link's font: the body face at the body text's size. */
reckless::gfx::Font LinkFont()
{
    return fonts::Small().WithHeight(17.0f);
}

/**
 * The content's height for the engines listed.
 * @param engines How many.
 * @return In design pixels.
 */
int ContentHeight(std::size_t engines)
{
    int height = 4 + kMarkSide + kTermsGap + kLine;
    if (engines > 0)
    {
        height += kSectionGap + kLabelHeight + kLine * static_cast<int>(engines) + kLine;
    }
    return height + kSectionGap + kNoticeHeight + 4;
}

/**
 * The card's height for the engines listed.
 * @param engines How many.
 * @return In design pixels.
 */
int CardHeightFor(std::size_t engines)
{
    return kTitleBand + ContentHeight(engines) + kFooterBand;
}

/**
 * An engine's name and version.
 * @param engine The engine.
 * @return "Pawnstar 1.2.0".
 */
std::string NameAndVersion(const AboutEngine &engine)
{
    return engine.version.empty() ? engine.name : engine.name + " " + engine.version;
}

} // namespace

std::string LicenceName(const std::string &spdx)
{
    if (spdx == "GPL-3.0" || spdx == "GPL-3.0-only")
    {
        return "GPLv3";
    }
    if (spdx == "GPL-3.0-or-later" || spdx == "GPL-3.0+")
    {
        return "GPLv3 or later";
    }
    return spdx;
}

LinkLabel::LinkLabel(std::string text) : text_(std::move(text))
{
    cursor = reckless::view::Cursor::kPointingHand;
    SetOpaque(false);
}

void LinkLabel::Paint(reckless::gfx::Canvas &c)
{
    c.SetFont(LinkFont());
    c.SetColor(theme::kAmber);
    c.DrawText(text_, LocalBounds().ToFloat(), Justify::kCentredLeft, true);
    if (is_over_)
    {
        const float underline = std::min(c.TextWidth(LinkFont(), text_), static_cast<float>(width()));
        const float y         = static_cast<float>(height()) - 4.0f;
        c.DrawLine({0.0f, y}, {underline, y}, 1.0f);
    }
}

void LinkLabel::MouseEnter(const reckless::view::MouseEvent &e)
{
    static_cast<void>(e);
    is_over_ = true;
    Repaint();
}

void LinkLabel::MouseExit(const reckless::view::MouseEvent &e)
{
    static_cast<void>(e);
    is_over_ = false;
    Repaint();
}

void LinkLabel::MouseUp(const reckless::view::MouseEvent &e)
{
    if (LocalBounds().ToFloat().Contains(e.position) && on_click)
    {
        on_click();
    }
}

AboutDialog::AboutDialog(AboutInfo info, const PieceSet &pieces)
    : DialogFrame("About Reckless UCI Host", kCardWidth, CardHeightFor(info.engines.size())), info_(std::move(info)),
      pieces_(pieces), link_("recklesstechnology.com")
{
    link_.on_click = [this] {
        if (open_url)
        {
            open_url(kWebsite);
        }
    };
    AddChild(link_);
    AddButton("Close", [this] {
        if (on_close)
        {
            on_close();
        }
    });
    on_accept = [this] {
        if (on_close)
        {
            on_close();
        }
    };
    on_cancel = on_accept;
}

std::vector<std::string> AboutDialog::EngineLines() const
{
    std::vector<std::string> lines;
    for (const AboutEngine &engine : info_.engines)
    {
        const std::string licence = LicenceName(engine.licence);
        lines.push_back(licence.empty() ? NameAndVersion(engine) : NameAndVersion(engine) + " (" + licence + ")");
    }
    return lines;
}

std::vector<std::string> AboutDialog::Lines() const
{
    std::vector<std::string> lines = {kName, "Version " + info_.version, kCopyright, link_.text(), kTerms};
    if (!info_.engines.empty())
    {
        lines.push_back("Bundled engines");
        for (std::string &line : EngineLines())
        {
            lines.push_back(std::move(line));
        }
        lines.push_back(kEngineNote);
    }
    lines.push_back(kNotices);
    return lines;
}

void AboutDialog::LayOutContent(RectI area)
{
    area_            = area;
    const int left   = area.x() + kTextLeft;
    const int top    = area.y() + 4;
    const int width  = static_cast<int>(std::ceil(reckless::gfx::MeasureText(LinkFont(), link_.text()))) + 4;
    const int link_y = top + kCopyrightTop + kLine; // the line under the copyright
    link_.SetBounds({left, link_y, std::min(width, area.right() - left), kLine});
}

void AboutDialog::Paint(reckless::gfx::Canvas &c)
{
    DialogFrame::Paint(c);
    const RectI area      = area_;
    const float left      = static_cast<float>(area.x());
    const float text_left = left + static_cast<float>(kTextLeft);
    const float right     = static_cast<float>(area.right());
    float       y         = static_cast<float>(area.y()) + 4.0f;

    // The mark: the black queen on a white square, as the app's icon draws it.
    const RectF mark{left, y, static_cast<float>(kMarkSide), static_cast<float>(kMarkSide)};
    c.SetColor(theme::kWhiteSide);
    c.FillRoundedRect(mark, 14.0f);
    pieces_.Draw(c, chess::kBlack, chess::kQueen, mark.Reduced(10.0f));

    c.SetFont(fonts::Body().WithHeight(28.0f).Bold());
    c.SetColor(theme::kInk);
    c.DrawText(kName, RectF{text_left, y, right - text_left, static_cast<float>(kNameHeight)}, Justify::kCentredLeft,
               true);
    c.SetFont(fonts::Small());
    c.SetColor(theme::kMuted);
    c.DrawText("Version " + info_.version, RectF{text_left, y + kNameHeight, right - text_left, 22.0f},
               Justify::kCentredLeft, true);
    c.SetFont(fonts::Small().WithHeight(17.0f));
    c.SetColor(theme::kInk);
    c.DrawText(kCopyright, RectF{text_left, y + kCopyrightTop, right - text_left, static_cast<float>(kLine)},
               Justify::kCentredLeft, true);
    // The link, a line under the copyright, is a view of its own.

    y += kMarkSide + kTermsGap;
    c.SetColor(theme::kInk);
    c.DrawText(kTerms, RectF{left, y, right - left, static_cast<float>(kLine)}, Justify::kCentredLeft, true);
    y += kLine;

    if (!info_.engines.empty())
    {
        y += kSectionGap;
        c.SetColor(theme::kRule);
        c.DrawHorizontalLine(static_cast<int>(y) - 6, left, right);
        c.SetFont(fonts::Label());
        c.SetColor(theme::kMuted);
        c.DrawText("BUNDLED ENGINES", RectF{left, y, right - left, static_cast<float>(kLabelHeight)},
                   Justify::kCentredLeft, true);
        y += kLabelHeight;
        for (const AboutEngine &engine : info_.engines)
        {
            const RectF row{left, y, right - left, static_cast<float>(kLine)};
            c.SetFont(fonts::Small().WithHeight(17.0f));
            c.SetColor(theme::kInk);
            c.DrawText(NameAndVersion(engine), row.WithWidth(kLicenceLeft), Justify::kCentredLeft, true);
            c.SetColor(theme::kMuted);
            c.DrawText(LicenceName(engine.licence), row.WithTrimmedLeft(kLicenceLeft), Justify::kCentredLeft, true);
            y += kLine;
        }
        c.SetFont(fonts::Small());
        c.SetColor(theme::kMuted);
        c.DrawText(kEngineNote, RectF{left, y, right - left, static_cast<float>(kLine)}, Justify::kCentredLeft, true);
        y += kLine;
    }

    y += kSectionGap;
    c.SetColor(theme::kRule);
    c.DrawHorizontalLine(static_cast<int>(y) - 6, left, right);
    c.SetFont(fonts::Small());
    c.SetColor(theme::kMuted);
    // On one line, squeezed a little where a face draws it wider than the card.
    c.DrawFittedText(kNotices, RectI{area.x(), static_cast<int>(y), area.width(), kNoticeHeight}, Justify::kCentredLeft,
                     1, 0.85f);
}

} // namespace uci_host::app
