# 0087. DAWproject automation curves

## Context

DAWproject points are straight or held; Kalibre's automation segments can bend
(a curve each). They came back straight (0084).

## Decision

- A bent segment is written as short straight pieces along the bend (about a
  32nd note each, 4 to 32 a segment), so Bitwig, Studio One or Cubase follow
  its shape.
- The lane's own points, curves and all, also go in a side file only Kalibre
  reads (`kalibre/automation.json`, by the Points' id), as plug-in states do
  (0077). Opening the file in Kalibre puts the curves back exactly.
- The file still validates against the official schema.

## Consequences

Tested: written and read by another program's rules, the shape is within 0.02
of the curve; read by Kalibre, the points are exactly the same. A file edited
in another program and brought back keeps Kalibre's curves only where that
lane's Points still has its id: changes made elsewhere to that lane are lost
to the curves.
