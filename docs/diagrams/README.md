# Diagrams

This folder contains generated UML diagrams for the codebase.

## Generate (C++ class diagram for primitives)

From the repo root:

```bash
clang-uml -c clang-uml.yaml -n primitives_class
scripts/plantuml_layout.sh docs/diagrams/primitives_class.puml
```

Output:
- `docs/diagrams/primitives_class.puml`

The post-process step enforces vertical layout by replacing
`left to right direction` with `top to bottom direction` (or inserting it
if missing).

## View in VS Code

Install a PlantUML extension and open the `.puml` file to render.
If layout looks off, make sure Graphviz (`dot`) is installed.
