#pragma once

// Tiny i18n: tr(english) returns the Russian translation when the language is
// Russian and the string is in the dictionary, otherwise the English original.
// Format strings keep identical printf signatures in both languages.
namespace current_lab::i18n {

enum class Language { English, Russian };

void setLanguage(Language language);
Language language();
const char* tr(const char* english);

} // namespace current_lab::i18n
