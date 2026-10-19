#include "EngineCatalogue.h"

#include "JsonFile.h"
#include "PresetFiles.h"

#include <algorithm>
#include <cctype>

namespace uci_host::app
{

namespace
{

namespace json = reckless::core::json;

/** The catalogue file's format number, written and checked. */
constexpr int kFormat = 1;

/**
 * Whether two option names are the same, as UCI compares them.
 * @param a One name.
 * @param b The other.
 * @return Whether they match without regard to case.
 */
bool SameName(const std::string &a, const std::string &b)
{
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

/**
 * The option types by their UCI names.
 * @param name "check", "spin", "combo", "button" or "string".
 * @return The type; a string for any other name.
 */
uci::OptionType TypeNamed(const std::string &name)
{
    for (const uci::OptionType type : {uci::OptionType::kCheck, uci::OptionType::kSpin, uci::OptionType::kCombo,
                                       uci::OptionType::kButton, uci::OptionType::kString})
    {
        if (uci::OptionTypeName(type) == name)
        {
            return type;
        }
    }
    return uci::OptionType::kString;
}

/**
 * A list of text from an array, its elements that are not text skipped.
 * @param value The array, or anything else for none.
 * @return The text.
 */
std::vector<std::string> Strings(const json::Value &value)
{
    std::vector<std::string> out;
    if (const json::Array *array = value.AsArray())
    {
        for (const json::Value &element : *array)
        {
            if (element.IsString())
            {
                out.push_back(element.AsString());
            }
        }
    }
    return out;
}

/**
 * An engine from its object.
 * @param object The object.
 * @return The engine, or nothing with no executable.
 */
std::optional<EngineEntry> EntryFrom(const json::Value &object)
{
    EngineEntry entry;
    entry.config.executable = object.StringOr("executable", "");
    if (entry.config.executable.empty())
    {
        return std::nullopt;
    }
    entry.config.name              = object.StringOr("name", "");
    entry.config.working_directory = object.StringOr("working_directory", "");
    entry.config.arguments         = Strings(object["arguments"]);
    entry.config.notes             = object.StringOr("notes", "");
    entry.config.does_ponder       = object.BoolOr("ponder", false);
    if (const json::Array *options = object["options"].AsArray())
    {
        for (const json::Value &option : *options)
        {
            const std::string name = option.StringOr("name", "");
            if (!name.empty())
            {
                entry.config.options.emplace_back(name, option.StringOr("value", ""));
            }
        }
    }
    entry.author       = object.StringOr("author", "");
    entry.engine_name  = object.StringOr("engine_name", "");
    entry.book_path    = object.StringOr("book", "");
    entry.book_depth   = static_cast<int>(std::max<std::int64_t>(0, object.IntOr("book_depth", 0)));
    entry.is_analysing = object.BoolOr("analyse", true);
    if (const json::Value *depth = object.Find("depth"); depth != nullptr && depth->IsInt() && depth->AsInt() > 0)
    {
        entry.depth = static_cast<int>(depth->AsInt());
    }
    if (const json::Array *declared = object["declared"].AsArray())
    {
        for (const json::Value &option : *declared)
        {
            uci::EngineOption parsed;
            parsed.name = option.StringOr("name", "");
            if (parsed.name.empty())
            {
                continue;
            }
            parsed.type          = TypeNamed(option.StringOr("type", "string"));
            parsed.default_value = option.StringOr("default", "");
            if (const json::Value *minimum = option.Find("min"); minimum != nullptr && minimum->IsInt())
            {
                parsed.minimum = minimum->AsInt();
            }
            if (const json::Value *maximum = option.Find("max"); maximum != nullptr && maximum->IsInt())
            {
                parsed.maximum = maximum->AsInt();
            }
            parsed.choices = Strings(option["choices"]);
            entry.declared.push_back(std::move(parsed));
        }
    }
    return entry;
}

} // namespace

std::vector<std::string> SplitArguments(const std::string &text)
{
    std::vector<std::string> words;
    std::string              word;
    bool                     is_quoted = false;
    bool                     has_word  = false;
    for (const char character : text)
    {
        if (character == '"')
        {
            is_quoted = !is_quoted;
            has_word  = true;
        }
        else if (character == ' ' && !is_quoted)
        {
            if (has_word)
            {
                words.push_back(word);
            }
            word.clear();
            has_word = false;
        }
        else
        {
            word += character;
            has_word = true;
        }
    }
    if (has_word)
    {
        words.push_back(word);
    }
    return words;
}

std::string JoinArguments(const std::vector<std::string> &words)
{
    std::string text;
    for (const std::string &word : words)
    {
        text += (text.empty() ? "" : " ") +
                (word.find(' ') != std::string::npos || word.empty() ? "\"" + word + "\"" : word);
    }
    return text;
}

std::string EngineEntry::DisplayName() const
{
    if (!config.name.empty())
    {
        return config.name;
    }
    if (!engine_name.empty())
    {
        return engine_name;
    }
    return std::filesystem::path(config.executable).stem().string();
}

std::string EngineEntry::OptionValue(const std::string &name) const
{
    for (const auto &[option, value] : config.options)
    {
        if (SameName(option, name))
        {
            return value;
        }
    }
    for (const uci::EngineOption &option : declared)
    {
        if (SameName(option.name, name))
        {
            return option.default_value;
        }
    }
    return {};
}

void EngineEntry::SetOption(const std::string &name, const std::string &value)
{
    for (auto &[option, configured] : config.options)
    {
        if (SameName(option, name))
        {
            configured = value;
            return;
        }
    }
    config.options.emplace_back(name, value);
}

void EngineEntry::ClearOption(const std::string &name)
{
    auto &options = config.options;
    options.erase(std::remove_if(options.begin(), options.end(),
                                 [&name](const auto &option) { return SameName(option.first, name); }),
                  options.end());
}

bool EngineEntry::HasOption(const std::string &name) const
{
    return std::any_of(config.options.begin(), config.options.end(),
                       [&name](const auto &option) { return SameName(option.first, name); });
}

std::string EngineEntry::Summary() const
{
    std::vector<std::string> parts;
    for (const char *name : {"Threads", "Hash"})
    {
        const std::string value = OptionValue(name);
        if (!value.empty())
        {
            parts.push_back(std::string(name) + " " + value + (std::string(name) == "Hash" ? " MB" : ""));
        }
    }
    if (depth)
    {
        parts.push_back("depth " + std::to_string(*depth));
    }
    if (config.does_ponder)
    {
        parts.push_back("ponder");
    }
    if (!book_path.empty())
    {
        parts.push_back("book");
    }
    std::string text;
    for (const std::string &part : parts)
    {
        text += (text.empty() ? "" : ", ") + part;
    }
    return text;
}

std::filesystem::path EngineCatalogue::DefaultFile()
{
    const std::filesystem::path folder = reckless::core::UserSettingsFolder(reckless::core::kSuiteFolder);
    return folder.empty() ? folder : folder / "uci-host" / "engines.json";
}

json::Value EngineCatalogue::ToJson() const
{
    json::Value document = json::Value::MakeObject();
    document.Set("format", kFormat);
    json::Value list = json::Value::MakeArray();
    for (const EngineEntry &entry : engines)
    {
        json::Value object = json::Value::MakeObject();
        object.Set("name", entry.config.name);
        object.Set("executable", entry.config.executable);
        object.Set("working_directory", entry.config.working_directory);
        json::Value arguments = json::Value::MakeArray();
        for (const std::string &argument : entry.config.arguments)
        {
            arguments.Append(argument);
        }
        object.Set("arguments", std::move(arguments));
        json::Value options = json::Value::MakeArray();
        for (const auto &[name, value] : entry.config.options)
        {
            json::Value option = json::Value::MakeObject();
            option.Set("name", name);
            option.Set("value", value);
            options.Append(std::move(option));
        }
        object.Set("options", std::move(options));
        object.Set("notes", entry.config.notes);
        object.Set("ponder", entry.config.does_ponder);
        object.Set("book", entry.book_path);
        object.Set("book_depth", entry.book_depth);
        object.Set("depth", entry.depth ? json::Value(*entry.depth) : json::Value(nullptr));
        object.Set("analyse", entry.is_analysing);
        object.Set("author", entry.author);
        object.Set("engine_name", entry.engine_name);
        json::Value declared = json::Value::MakeArray();
        for (const uci::EngineOption &option : entry.declared)
        {
            json::Value item = json::Value::MakeObject();
            item.Set("name", option.name);
            item.Set("type", uci::OptionTypeName(option.type));
            item.Set("default", option.default_value);
            if (option.minimum)
            {
                item.Set("min", *option.minimum);
            }
            if (option.maximum)
            {
                item.Set("max", *option.maximum);
            }
            if (!option.choices.empty())
            {
                json::Value choices = json::Value::MakeArray();
                for (const std::string &choice : option.choices)
                {
                    choices.Append(choice);
                }
                item.Set("choices", std::move(choices));
            }
            declared.Append(std::move(item));
        }
        object.Set("declared", std::move(declared));
        list.Append(std::move(object));
    }
    document.Set("engines", std::move(list));
    return document;
}

std::optional<EngineCatalogue> EngineCatalogue::FromJson(const json::Value &document, std::string *error)
{
    const auto refuse = [error](std::string why) -> std::optional<EngineCatalogue> {
        if (error != nullptr)
        {
            *error = std::move(why);
        }
        return std::nullopt;
    };
    if (!document.IsObject())
    {
        return refuse("it is not a JSON object");
    }
    if (document.IntOr("format", 0) != kFormat)
    {
        return refuse("it is not an engine list of format " + std::to_string(kFormat));
    }
    EngineCatalogue catalogue;
    if (const json::Array *list = document["engines"].AsArray())
    {
        for (const json::Value &object : *list)
        {
            if (auto entry = EntryFrom(object))
            {
                catalogue.engines.push_back(std::move(*entry));
            }
        }
    }
    return catalogue;
}

std::optional<EngineCatalogue> EngineCatalogue::Load(const std::filesystem::path &file, std::string *error)
{
    std::error_code ignored;
    if (file.empty() || !std::filesystem::exists(file, ignored))
    {
        return EngineCatalogue{};
    }
    std::string                      why;
    const std::optional<json::Value> document = reckless::core::ReadJsonFile(file, &why);
    if (!document)
    {
        if (error != nullptr)
        {
            *error = file.string() + ": " + why;
        }
        return std::nullopt;
    }
    auto catalogue = FromJson(*document, &why);
    if (!catalogue && error != nullptr)
    {
        *error = file.string() + ": " + why;
    }
    return catalogue;
}

bool EngineCatalogue::Save(const std::filesystem::path &file) const
{
    return reckless::core::WriteJsonFile(file, ToJson());
}

} // namespace uci_host::app
