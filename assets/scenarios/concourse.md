# Concourse scenario

```text
96 m × 54 m

west staging area        central service block     fare line       exits

+--------------------+        +------+                |             E0
|                    |        |      |       gate 0   |              >
| entrance/spawn     |        |      |        =======+
|                    |        |      |                |
|                    |        |      |                |   fixtures
|                    |        |      |                |   --------
|                    |        |      |                |   --------
|                    |        |      |        =======+
|                    |        +------+       gate 1   |              >
+--------------------+                                |             E1
```

Agents spawn in the west concourse with one of two east-side goals. The fare
line has two narrow traversable openings. The demo closes the upper gate at a
configured tick, invalidating both shared flow fields and forcing affected
agents toward the lower route.

The map is built in `Simulation::build_scenario()` so navigation behavior is
fully reproducible without an asset parser. Moving it to a versioned data file
is presentation-layer roadmap work.
