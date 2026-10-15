// The setup speaking for itself (setup_voice.cpp): a screen reader or the system's voice through Prism, in the
// language of the game's screen reader, and the disc image found in the setup's folder or the two above it.
#ifndef PORT_SETUP_VOICE_H
#define PORT_SETUP_VOICE_H
#include <string>
#include <vector>

void Voice_Start(const std::string &root);              // root: the release folder (with prism.dll and a11y_<code>.txt)
bool Voice_On(void);                                      // Prism started: there is someone to speak to
void Voice_Say(const std::string &text, bool interrupt);  // UTF-8; nothing without Prism
const char *Voice_Tr(const char *en);                     // the language's line for an English text, or the text
const char *Voice_TrFormat(const char *en);               // the same for a printf format: only a line with its conversions
std::vector<std::string> Voice_FindIsos(const std::string &root); // the disc images of root and the two folders above, best first

#endif
