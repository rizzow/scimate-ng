// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "scimate/resource_game.hpp"
#include <optional>
#include <iterator>
#include <string>
#include <utility>

namespace scimate::parser_detail {
enum class SaidOperator : unsigned {
    comma = 0xf0, conjunction = 0xf1, slash = 0xf2, open = 0xf3, close = 0xf4,
    optional_open = 0xf5, optional_close = 0xf6, hash = 0xf7, reference = 0xf8,
    partial = 0xf9, end = 0xff
};
namespace said_tag {
inline constexpr unsigned generic = 0x141, subject = 0x142, suffix = 0x143, reference = 0x144;
inline constexpr unsigned partial = 0x14b, optional = 0x152, root = 0x13f;
inline constexpr unsigned predicate_part = 0x149, other_part = 0x14a, entry = 0x14c;
inline constexpr unsigned alternatives = 0x14f, nested_reference = 0x144, word = 0x153;
inline constexpr unsigned other_alternatives = 0x150, ignored = 0x145;
}
namespace said_word { inline constexpr unsigned none = 0xffe, any = 0xfff; }
inline constexpr unsigned said_max_tokens = 128, said_max_group = 0xfff;
struct SaidToken { bool group; unsigned value; };
struct SaidTokens { std::vector<SaidToken> tokens; std::size_t bytes_consumed; };
[[noreturn]] inline void invalid_said(const char* message) { throw std::runtime_error(std::string("portable Said: ")+message); }
inline SaidTokens tokenize_said(const Bytes& bytes, std::size_t start, std::size_t end) {
    if (start > end || end > bytes.size()) invalid_said("invalid byte range");
    SaidTokens result{{},0};
    auto cursor = start;
    while (result.tokens.size() < said_max_tokens) {
        if (cursor == end) invalid_said("missing terminator");
        const auto first = bytes[cursor++];
        if (first < static_cast<unsigned>(SaidOperator::comma)) {
            if (cursor == end) invalid_said("truncated word group");
            const auto group = (static_cast<unsigned>(first) << 8) | bytes[cursor++];
            if (group > said_max_group) invalid_said("word group exceeds twelve bits");
            result.tokens.push_back({true,group});
        } else {
            if (first > static_cast<unsigned>(SaidOperator::partial) && first != static_cast<unsigned>(SaidOperator::end))
                invalid_said("unknown operator");
            result.tokens.push_back({false,first});
            if (first == static_cast<unsigned>(SaidOperator::end)) { result.bytes_consumed = cursor-start; return result; }
        }
    }
    invalid_said("token limit exceeded");
}
struct SaidNode {
    unsigned major, minor;
    std::optional<unsigned> word;
    std::vector<SaidNode> children;
};
// A tagged, n-ary representation of the format's semantic tree. Keeping these
// wrappers distinguishes optional absence from a present but mismatching word.
class SaidCompiler {
public:
    explicit SaidCompiler(const std::vector<SaidToken>& tokens) : tokens_(tokens) {
        if (tokens.empty() || tokens.size() > said_max_tokens) invalid_said("invalid token count");
    }
    SaidNode compile() {
        SaidNode root{said_tag::generic,said_tag::root,std::nullopt,{}};
        if (auto predicate = expression()) root.children.push_back(wrap(said_tag::generic,said_tag::predicate_part,std::move(*predicate)));
        bool found_part = false;
        if (auto subject = part(said_tag::subject,found_part)) root.children.push_back(std::move(*subject));
        if (found_part) { bool suffix_found = false; if (auto suffix = part(said_tag::suffix,suffix_found)) root.children.push_back(std::move(*suffix)); }
        if (root.children.empty() && !found_part) invalid_said("empty or invalid specification");
        if (take(SaidOperator::partial)) root.children.push_back({said_tag::partial,
            static_cast<unsigned>(SaidOperator::partial) << 8,static_cast<unsigned>(SaidOperator::partial) << 8,{}});
        if (!take(SaidOperator::end) || cursor_ != tokens_.size()) invalid_said("invalid or trailing specification tokens");
        return root;
    }
private:
    const std::vector<SaidToken>& tokens_;
    std::size_t cursor_ = 0;
    bool take(SaidOperator token) {
        if (cursor_ == tokens_.size()) return false;
        const auto& current = tokens_[cursor_];
        if (current.group || current.value != static_cast<unsigned>(token)) return false;
        ++cursor_; return true;
    }
    static SaidNode wrap(unsigned major, unsigned minor, std::vector<SaidNode> children) {
        return {major,minor,std::nullopt,std::move(children)};
    }
    std::optional<SaidNode> entry() {
        const auto save = cursor_;
        for (const auto& bracket : {std::pair{SaidOperator::optional_open,SaidOperator::optional_close},
                                   std::pair{SaidOperator::open,SaidOperator::close}}) {
            if (take(bracket.first)) {
                auto inner = expression();
                if (inner && take(bracket.second)) return wrap(bracket.first == SaidOperator::optional_open ?
                    said_tag::optional : said_tag::generic,said_tag::entry,std::move(*inner));
                cursor_ = save;
            }
        }
        if (cursor_ < tokens_.size() && tokens_[cursor_].group) {
            const auto group = tokens_[cursor_++].value;
            return SaidNode{said_tag::generic,said_tag::word,group,{}};
        }
        return std::nullopt;
    }
    std::optional<std::vector<SaidNode>> list() {
        auto first = entry(); if (!first) return std::nullopt;
        std::vector<SaidNode> nodes; nodes.push_back(std::move(*first));
        for (;;) {
            const auto save = cursor_;
            if (!take(SaidOperator::comma)) break;
            auto next = entry();
            if (!next) { cursor_ = save; break; }
            nodes.push_back(std::move(*next));
        }
        return nodes;
    }
    std::optional<std::vector<SaidNode>> references() {
        const auto save = cursor_;
        if (take(SaidOperator::reference)) {
            if (auto words = list()) {
                std::vector<SaidNode> nodes;
                nodes.push_back(wrap(said_tag::reference,said_tag::alternatives,std::move(*words)));
                if (auto rest = references()) nodes.push_back(wrap(said_tag::generic,said_tag::nested_reference,std::move(*rest)));
                return nodes;
            }
        }
        if (take(SaidOperator::optional_open)) {
            auto inner = references();
            if (inner && take(SaidOperator::optional_close))
                return std::vector<SaidNode>{wrap(said_tag::optional,said_tag::reference,std::move(*inner))};
        }
        cursor_ = save; return std::nullopt;
    }
    std::optional<std::vector<SaidNode>> expression() {
        const auto save = cursor_;
        std::vector<SaidNode> nodes;
        if (auto words = list()) nodes.push_back(wrap(said_tag::generic,said_tag::alternatives,std::move(*words)));
        if (auto refs = references()) nodes.insert(nodes.end(),std::make_move_iterator(refs->begin()),std::make_move_iterator(refs->end()));
        if (!nodes.empty()) return nodes;
        cursor_ = save; return std::nullopt;
    }
    std::optional<SaidNode> part(unsigned major, bool& found) {
        const auto save = cursor_; found = false;
        if (take(SaidOperator::slash)) {
            found = true;
            if (auto inner = expression()) return wrap(major,said_tag::other_part,std::move(*inner));
            return std::nullopt; // An empty slash advances to the next sentence part.
        }
        if (take(SaidOperator::optional_open)) {
            bool inner_found = false; auto inner = part(major,inner_found);
            if (inner_found && take(SaidOperator::optional_close)) {
                found = true;
                std::vector<SaidNode> children; if (inner) children.push_back(std::move(*inner));
                return wrap(said_tag::optional,major,std::move(children));
            }
        }
        cursor_ = save; return std::nullopt;
    }
};
inline SaidNode compile_said(const Bytes& bytes, std::size_t start, std::size_t end) {
    const auto decoded = tokenize_said(bytes,start,end);
    return SaidCompiler(decoded.tokens).compile();
}
} // namespace scimate::parser_detail
