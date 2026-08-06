#pragma once

#include <QByteArray>

#include <ghostty.h>

#include <cstdint>

class QKeyEvent;

[[nodiscard]] uint32_t ghosttyUnshiftedCodepoint(const QKeyEvent& event);
[[nodiscard]] QByteArray ghosttyKeyText(const QKeyEvent& event,
                                        uint32_t unshiftedCodepoint);
[[nodiscard]] bool ghosttyShouldSendKeyRelease(const QKeyEvent& event);
[[nodiscard]] ghostty_input_key_e ghosttyLogicalKey(const QKeyEvent& event);
