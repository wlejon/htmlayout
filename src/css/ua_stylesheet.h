#pragma once
#include "css/parser.h"

namespace htmlayout::css {

// Returns a pre-parsed user-agent default stylesheet.
// This provides sensible defaults for HTML elements (block display for div/p/h1-h6,
// bold for headings, margins, etc.) so that unstyled HTML renders reasonably.
const Stylesheet& defaultUserAgentStylesheet();

// Returns the raw CSS string of the user-agent stylesheet.
const std::string& defaultUserAgentCSS();

} // namespace htmlayout::css
