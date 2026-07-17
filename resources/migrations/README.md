# Database migrations

Migration SQL is currently compiled into `DatabaseManager.cpp` so every executable applies the same
transactional sequence. Move a migration here only when its loader and integrity test are added in the
same change.
