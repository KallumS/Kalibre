# 0025. Messages at the top, until dismissed

## Context

Apple: nothing important at a window's bottom edge, and nothing that times out.

## Decision

One message line under the transport, shown only when there's something to say. It stays until dismissed or replaced, and can Undo what it describes.

Amended by 0070: no Dismiss; a message gives way when the choice or the pointer moves on, and between messages the line tells what's chosen.
