# 0120. The lanes under the ruler stay uncovered

## Context

Reported (22 November): scrolling the tracks down, each track's header slid up
over the Sections and Chords lanes under the ruler: untidy, and the lanes'
names were hidden. The lanes themselves were drawn clipped; the headers,
components of their own, were placed in the whole arrangement.

## Decision

The headers sit in a column of their own that starts under the ruler and its
lanes (`Arrange::headerColumn`): a header scrolled up is cut off at the
column's top. Clicks still reach the headers; the column itself takes none.

## Consequences

Tested (failing first): with twelve tracks scrolled down, no header shows
above the lanes' bottom.
