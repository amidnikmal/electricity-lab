#pragma once

// Tiny i18n: tr(english) returns the Russian translation when the language is
// Russian and the string is in the dictionary, otherwise the English original.
// Format strings keep identical printf signatures in both languages.
namespace current_lab::i18n {

enum class Language { English, Russian };

void setLanguage(Language language);
Language language();
const char* tr(const char* english);

// Every dictionary key and value concatenated: feed this to the font-atlas
// builder so no UI string can ever render as "?".
const char* allUiText();

} // namespace current_lab::i18n
