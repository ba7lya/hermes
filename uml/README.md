# UML class diagrams

PlantUML sources for the rewritten library. Render one with

```sh
plantuml uml/<file>.uml
```

| File | Contents |
| --- | --- |
| [`hermes.uml`](hermes.uml) | the public API of `ba7lya::hermes`: the protocol vocabulary, the message structs and the `any_message` variant, the codec, the settings and the six facades |
| [`sessions.uml`](sessions.uml) | the internals of `ba7lya::hermes::detail`: the I/O runner, the framed connection, the connector and acceptor, the three session types, the two state charts and their facade implementations |

The diagrams are sources only; render them locally to produce an image. See
[`doc/architecture.md`](../doc/architecture.md) for the prose description of the same layering.
