// Copyright (c) 2026 David Bertet. Licensed under the MIT License.

// Which of this product's messages must never be queued while the WebSocket
// is down and replayed on reconnect (see src/core/lib/wsqueue.js). Core ships
// only the transport-level blocklist (wifi); everything physical belongs
// here, because core cannot know what a product actuates.
import { registerNonQueuableTypes } from 'src/core/lib/wsqueue.js'

// A stale door/light/lock command replayed after a controller restart would
// move the door by itself.
registerNonQueuableTypes(['door_command', 'light_command', 'lock_command'])
