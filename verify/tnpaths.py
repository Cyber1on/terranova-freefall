"""Where the verification scripts find the original game.

TN_GAME_ROOT = the folder that holds 01_Game/TNOVA (the game) and, for some frame tests,
01_Game/CD_MAPS.  Default: a folder named 'Terra Nova' next to the repository folder."""
import os
HERE = os.path.dirname(os.path.abspath(__file__))
GAME_ROOT = os.path.abspath(os.environ.get('TN_GAME_ROOT') or os.path.join(HERE, '..', '..', 'Terra Nova'))
