// Запасная часть модуля: без miniaudio (нет vcpkg-функции audio) звукового движка нет, hasAudio() возвращает false.
// Код, вызывающий AudioSystem, оборачивают в #ifdef HAN_HAS_MINIAUDIO (макрос задаёт CMake модуля).
#include "han/audio/audio_system.h"

namespace han::audio {
bool hasAudio() { return false; }
}  // namespace han::audio
