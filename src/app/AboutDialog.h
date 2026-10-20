#pragma once
/// @file AboutDialog.h Help > About Reckless Amphitheater: the app's mark - its icon, Jonny's sketch of the Colosseum
/// on a rounded square - its name and version, the copyright, the terms it is used on, the engines the install carries
/// with their versions and licences, where the third-party notices are, and the website as a link to the person's
/// browser.

#include "Controls.h"

#include "Canvas.h"

#include <functional>
#include <string>
#include <vector>

namespace amphitheater::app
{

/** One engine the install carries, as the About window names it. */
struct AboutEngine
{
    std::string name;    ///< Its name ("Pawnstar").
    std::string version; ///< Its release's version ("1.2.0").
    std::string licence; ///< Its licence's SPDX id ("GPL-3.0"); empty where the manifest names none.
};

/** What the About window says. */
struct AboutInfo
{
    std::string              version; ///< The host's version, MAJOR.MINOR.BUILD.
    std::vector<AboutEngine> engines; ///< The engines the install carries; none for a build tree's host.
};

/**
 * A licence as a person reads it.
 * @param spdx Its SPDX id: "GPL-3.0" and "GPL-3.0-or-later" are named for the GPL's version; any other is shown as it
 *             is.
 * @return "GPLv3", "GPLv3 or later", or the id itself.
 */
std::string LicenceName(const std::string &spdx);

/** A line of text in the accent that does something when it is clicked: the About window's website. */
class LinkLabel final : public reckless::view::View
{
  public:
    /** @param text The text. */
    explicit LinkLabel(std::string text);

    std::function<void()> on_click; ///< Clicked.

    /** @return The text. */
    const std::string &text() const
    {
        return text_;
    }

    /**
     * The text in the accent, underlined while the pointer is over it.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

    /**
     * Underlines it.
     * @param e The pointer's arrival.
     */
    void MouseEnter(const reckless::view::MouseEvent &e) override;

    /**
     * Takes the underline away.
     * @param e The pointer's leaving.
     */
    void MouseExit(const reckless::view::MouseEvent &e) override;

    /**
     * A release over it clicks it.
     * @param e The release.
     */
    void MouseUp(const reckless::view::MouseEvent &e) override;

  private:
    std::string text_;            ///< The text.
    bool        is_over_ = false; ///< The pointer is over it.
};

/** The About window: a card over the window that a Close button, Escape or Return closes. */
class AboutDialog final : public DialogFrame
{
  public:
    static constexpr const char *kWebsite = "https://recklesstechnology.com"; ///< The page the link opens.

    /** The embedded file the mark is drawn from: the icon's 256-pixel render (src/app/CMakeLists.txt). */
    static constexpr const char *kMarkFile = "icon-256.png";

    /** @param info What it says. */
    explicit AboutDialog(AboutInfo info);

    std::function<void()>                       on_close; ///< Closed.
    std::function<void(const std::string &url)> open_url; ///< Opens a page in the person's browser.

    /** @return What it says. */
    const AboutInfo &info() const
    {
        return info_;
    }

    /** @return The lines it says, top to bottom, the engines' among them: what the suites read. */
    std::vector<std::string> Lines() const;

    /** @return The engines' lines, "Pawnstar 1.2.0 (GPLv3)"; none when the install carries none. */
    std::vector<std::string> EngineLines() const;

    /** @return Whether the mark was read from the binary: without it the card leaves its place empty. */
    bool has_mark() const
    {
        return mark_.IsValid();
    }

    /** @return The website's link. */
    LinkLabel &link()
    {
        return link_;
    }

    /**
     * The card, then the mark and the text on it.
     * @param c The canvas.
     */
    void Paint(reckless::gfx::Canvas &c) override;

  protected:
    void LayOutContent(reckless::gfx::RectI area) override;

  private:
    AboutInfo            info_; ///< What it says.
    reckless::gfx::Image mark_; ///< The app's icon.
    LinkLabel            link_; ///< The website.
    reckless::gfx::RectI area_; ///< Where the content goes, as last laid out.
};

} // namespace amphitheater::app
