#include <doctest/doctest.h>

#include <optional>

#include "core/commands.h"
#include "core/zoom_keys.h"

using rawreel::chord;
using rawreel::Key;
using rawreel::ZoomKey;
using rawreel::ZoomKeyAction;
using rawreel::zoomKeyAction;

TEST_CASE("zoom keys: the table") {
    CHECK(zoomKeyAction(chord(static_cast<Key>('F'))) == ZoomKeyAction{ZoomKey::ToggleMode});
    CHECK(zoomKeyAction(chord(static_cast<Key>('S'))) == ZoomKeyAction{ZoomKey::ToggleLayout});
    CHECK(zoomKeyAction(chord(static_cast<Key>('C'))) == ZoomKeyAction{ZoomKey::CentreOnPin});
    CHECK(zoomKeyAction(chord(static_cast<Key>('P'), false, true)) ==
          ZoomKeyAction{ZoomKey::LiveProbe});
    CHECK_FALSE(zoomKeyAction(chord(static_cast<Key>('P'))));
    CHECK(zoomKeyAction(chord(Key::Left)) == ZoomKeyAction{ZoomKey::MovePin, -1, 0, false});
    CHECK(zoomKeyAction(chord(Key::Right)) == ZoomKeyAction{ZoomKey::MovePin, 1, 0, false});
    CHECK(zoomKeyAction(chord(Key::Up)) == ZoomKeyAction{ZoomKey::MovePin, 0, -1, false});
    CHECK(zoomKeyAction(chord(Key::Down)) == ZoomKeyAction{ZoomKey::MovePin, 0, 1, false});
    CHECK(zoomKeyAction(chord(Key::Left, false, true)) ==
          ZoomKeyAction{ZoomKey::MovePin, -1, 0, true});
    CHECK(zoomKeyAction(chord(Key::Down, false, true)) ==
          ZoomKeyAction{ZoomKey::MovePin, 0, 1, true});
    CHECK(zoomKeyAction(chord(Key::Space)) == ZoomKeyAction{ZoomKey::PlayPause});
    CHECK_FALSE(zoomKeyAction(chord(static_cast<Key>('C'), true)));
    CHECK_FALSE(zoomKeyAction(chord(static_cast<Key>('X'))));
    CHECK_FALSE(zoomKeyAction(chord(Key::Escape)));
}
