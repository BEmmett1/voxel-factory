"""modelkit: build, preview and check block shapes and creatures without the game.

  spec.py     describe a model in Python -> .bbmodel (+ the kPartAnims rows)
  render.py   a software renderer that loads models exactly as the game does
  preview.py  CLI: four views, animation strips/GIFs, and the checks

See models/AUTHORING.md ("modelkit") for the workflow.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from spec import Model, Clip, atlas_tile, material, solid  # noqa: E402,F401
