#include "css/ua_stylesheet.h"

namespace htmlayout::css {

const std::string& defaultUserAgentCSS() {
    static const std::string css = R"CSS(
/* Block-level elements */
html, body, div, article, section, nav, aside, header, footer, main,
address, blockquote, figure, figcaption, details, summary,
hgroup, search {
    display: block;
}

h1, h2, h3, h4, h5, h6 {
    display: block;
    font-weight: bold;
}

p, pre, dl, ol, ul, menu, dir {
    display: block;
}

hr {
    display: block;
    border-top-style: solid;
    border-top-width: 1px;
    border-top-color: gray;
    margin-top: 8px;
    margin-bottom: 8px;
}

/* Headings: sizes and margins */
h1 {
    font-size: 32px;
    margin-top: 21px;
    margin-bottom: 21px;
}

h2 {
    font-size: 24px;
    margin-top: 19px;
    margin-bottom: 19px;
}

h3 {
    font-size: 19px;
    margin-top: 18px;
    margin-bottom: 18px;
}

h4 {
    font-size: 16px;
    margin-top: 21px;
    margin-bottom: 21px;
}

h5 {
    font-size: 13px;
    margin-top: 22px;
    margin-bottom: 22px;
}

h6 {
    font-size: 11px;
    margin-top: 24px;
    margin-bottom: 24px;
}

/* Paragraphs and body */
p {
    margin-top: 16px;
    margin-bottom: 16px;
}

body {
    margin-top: 8px;
    margin-right: 8px;
    margin-bottom: 8px;
    margin-left: 8px;
}

/* Lists */
ul, ol, menu, dir {
    padding-left: 40px;
    margin-top: 16px;
    margin-bottom: 16px;
}

li {
    display: list-item;
}

/* Preformatted text */
pre {
    white-space: pre;
    font-family: monospace;
    margin-top: 16px;
    margin-bottom: 16px;
}

code, kbd, samp, tt {
    font-family: monospace;
}

/* Inline formatting */
b, strong {
    font-weight: bold;
}

i, em, cite, var, dfn {
    font-style: italic;
}

small {
    font-size: 13px;
}

sub, sup {
    font-size: 12px;
    vertical-align: baseline;
}

/* Blockquote */
blockquote {
    margin-top: 16px;
    margin-bottom: 16px;
    margin-left: 40px;
    margin-right: 40px;
}

/* Tables — basic display (no full table layout) */
table {
    display: block;
    border-top-style: solid;
    border-right-style: solid;
    border-bottom-style: solid;
    border-left-style: solid;
    border-top-width: 1px;
    border-right-width: 1px;
    border-bottom-width: 1px;
    border-left-width: 1px;
    border-top-color: gray;
    border-right-color: gray;
    border-bottom-color: gray;
    border-left-color: gray;
}

/* Links */
a {
    color: blue;
    text-decoration: underline;
    cursor: pointer;
}

/* Hidden elements */
head, title, meta, link, style, script, noscript, template {
    display: none;
}

/* Form elements — basic inline display */
input, button, select, textarea {
    display: inline-block;
}

button {
    cursor: pointer;
}

/* Fieldset */
fieldset {
    display: block;
    margin-top: 0;
    margin-bottom: 0;
    margin-left: 2px;
    margin-right: 2px;
    padding-top: 6px;
    padding-right: 12px;
    padding-bottom: 12px;
    padding-left: 12px;
    border-top-style: solid;
    border-right-style: solid;
    border-bottom-style: solid;
    border-left-style: solid;
    border-top-width: 2px;
    border-right-width: 2px;
    border-bottom-width: 2px;
    border-left-width: 2px;
    border-top-color: gray;
    border-right-color: gray;
    border-bottom-color: gray;
    border-left-color: gray;
}

legend {
    display: block;
    padding-left: 2px;
    padding-right: 2px;
}

/* Image */
img {
    display: inline-block;
}
)CSS";
    return css;
}

const Stylesheet& defaultUserAgentStylesheet() {
    static const Stylesheet sheet = parse(defaultUserAgentCSS());
    return sheet;
}

} // namespace htmlayout::css
