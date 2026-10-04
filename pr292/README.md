# PR292 read-only inspector checks

Candidate `5de322b8d239aace17f7f6914e886eba04e74b6e`, parent `b4d69b2440a33ad93987b269c2ecac97c45367bc`. Ten actual helper tests pass; changing exclusive creation to ordinary write fails both overwrite/symlink refusal checks. Original first-red API-absence errors are retained as development history, not a reproduced native failure. No live GDB or whole traversal qualification is claimed. Existing native observations are separately preserved on `archive/pr292-before-review-20261004`; the current three-file merge candidate contains no raw evidence.
