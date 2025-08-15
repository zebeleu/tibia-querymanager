# Tibia 7.7 Query Manager
This is a simple query manager designed to support [Tibia Game Server](https://github.com/fusion32/tibia-game), [Tibia Login Server](https://github.com/fusion32/tibia-login), and [Tibia Web Server](https://github.com/fusion32/tibia-web). It uses the SQLite3 3.50.2 amalgamation for a database backend so we can completely avoid having to spin up yet another service for a separate database system, effectively turning the query manager into the database itself. This design choice was made with a single game server in mind. The networking protocol is NOT encrypted at all and won't accept remote connections. For a fully multi-world distributed infrastructure, a distributed database system like PostgreSQL and MySQL/MariaDB should be considered.

## Compiling
Even though there are no Linux specific features being used, it will currently only compile on Linux. It should be simple enough to support compiling on Windows but I don't think it would add any value, considering the game server needs to run on Linux and they need to be both on the same machine. The makefile is very simple and there are **ZERO** dependencies required. You can add the `-j N` switch to make it compile across N processes.
```
make                # build in release mode
make DEBUG=1        # build in debug mode
make clean          # remove `build` directory
```

## Running
The query manager will automatically manage the database schema based on files in `sql/` (see `sql/README.txt`), but won't automatically insert any initial data (see `sql/init.sql`). It does have a few configuration options that are loaded from `config.cfg` but the defaults should work for most use cases.

It is recommended that the query manager is setup as a service. There is a *systemd* configuration file (`tibia-querymanager.service`) in the repository that may be used for that purpose. The process is very similar to the one described in the [Game Server](https://github.com/fusion32/tibia-game) so I won't repeat myself here.
