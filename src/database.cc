#include "querymanager.hh"
#include "sqlite3.h"

struct TCachedStatement{
	sqlite3_stmt *Stmt;
	int64 LastUsed;
	uint32 Hash;
};

static sqlite3 *g_Database = NULL;
static TCachedStatement *g_CachedStatements = NULL;

// NOTE(fusion): SQLite's application id. We're currently setting it to ASCII
// "TiDB" for "Tibia Database".
constexpr int g_ApplicationID = 0x54694442;

// Statement Cache
//==============================================================================
uint32 HashText(const char *Text){
	// FNV1a 32-bits
	uint32 Hash = 0x811C9DC5U;
	for(int i = 0; Text[i] != 0; i += 1){
		Hash ^= (uint32)Text[i];
		Hash *= 0x01000193U;
	}
	return Hash;
}

sqlite3_stmt *PrepareQuery(const char *Text){
	sqlite3_stmt *Stmt = NULL;
	int LeastRecentlyUsed = 0;
	int64 LeastRecentlyUsedTime = g_CachedStatements[0].LastUsed;
	uint32 Hash = HashText(Text);
	for(int i = 0; i < g_MaxCachedStatements; i += 1){
		TCachedStatement *Entry = &g_CachedStatements[i];

		if(Entry->LastUsed < LeastRecentlyUsedTime){
			LeastRecentlyUsed = i;
			LeastRecentlyUsedTime = Entry->LastUsed;
		}

		if(Entry->Stmt != NULL && Entry->Hash == Hash){
			const char *EntryText = sqlite3_sql(Entry->Stmt);
			ASSERT(EntryText != NULL);
			if(strcmp(EntryText, Text) == 0){
				Stmt = Entry->Stmt;
				Entry->LastUsed = g_MonotonicTimeMS;
				break;
			}
		}
	}

	if(Stmt == NULL){
		if(sqlite3_prepare_v3(g_Database, Text, -1,
				SQLITE_PREPARE_PERSISTENT, &Stmt, NULL) != SQLITE_OK){
			LOG_ERR("Failed to prepary query: %s", sqlite3_errmsg(g_Database));
			return NULL;
		}

		TCachedStatement *Entry = &g_CachedStatements[LeastRecentlyUsed];
		if(Entry->Stmt != NULL){
			sqlite3_finalize(Entry->Stmt);
		}

		Entry->Stmt = Stmt;
		Entry->LastUsed = g_MonotonicTimeMS;
		Entry->Hash = Hash;
	}else{
		sqlite3_reset(Stmt);
		sqlite3_clear_bindings(Stmt);
	}

	return Stmt;
}

bool InitStatementCache(void){
	ASSERT(g_CachedStatements == NULL);
	g_CachedStatements = (TCachedStatement*)calloc(
			g_MaxCachedStatements, sizeof(TCachedStatement));
	return true;
}

void ExitStatementCache(void){
	if(g_CachedStatements != NULL){
		for(int i = 0; i < g_MaxCachedStatements; i += 1){
			TCachedStatement *Entry = &g_CachedStatements[i];
			if(Entry->Stmt != NULL){
				sqlite3_finalize(Entry->Stmt);
				Entry->Stmt = NULL;
			}

			Entry->LastUsed = 0;
			Entry->Hash = 0;
		}

		free(g_CachedStatements);
		g_CachedStatements = NULL;
	}
}

// Queries
//==============================================================================
bool LoadWorldID(const char *WorldName, int *WorldID){
	ASSERT(WorldName && WorldID);
	sqlite3_stmt *Stmt = PrepareQuery(
			"SELECT WorldID FROM Worlds WHERE Name = ?1");
	if(Stmt == NULL){
		LOG_ERR("Failed to prepare query");
		return false;
	}

	if(sqlite3_bind_text(Stmt, 1, WorldName, -1, NULL) != SQLITE_OK){
		LOG_ERR("Failed to bind WorldName: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	if(sqlite3_step(Stmt) != SQLITE_ROW){
		LOG_ERR("Failed to execute query: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	*WorldID = sqlite3_column_int(Stmt, 0);
	return true;
}

bool LoadHouseOwners(int WorldID, DynamicArray<THouseOwner> *HouseOwners){
	ASSERT(HouseOwners);
	sqlite3_stmt *Stmt = PrepareQuery(
			"SELECT O.HouseID, O.OwnerID, C.Name, O.PaidUntil"
			" FROM HouseOwners AS O"
			" LEFT JOIN Characters AS C ON C.CharacterID = O.OwnerID"
			" WHERE O.WorldID = ?1");
	if(Stmt == NULL){
		LOG_ERR("Failed to prepare query");
		return false;
	}

	if(sqlite3_bind_int(Stmt, 1, WorldID) != SQLITE_OK){
		LOG_ERR("Failed to bind WorldID: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	while(sqlite3_step(Stmt) == SQLITE_ROW){
		THouseOwner Owner = {};
		Owner.HouseID = sqlite3_column_int(Stmt, 0);
		Owner.OwnerID = sqlite3_column_int(Stmt, 1);
		StringCopy(Owner.OwnerName, sizeof(Owner.OwnerName),
				(const char*)sqlite3_column_text(Stmt, 2));
		Owner.PaidUntil = sqlite3_column_int(Stmt, 3);
		HouseOwners->Push(Owner);
	}

	if(sqlite3_errcode(g_Database) != SQLITE_DONE){
		LOG_ERR("Failed to execute query: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	return true;
}

bool DeleteHouses(int WorldID){
	sqlite3_stmt *Stmt = PrepareQuery(
			"DELETE FROM Houses WHERE WorldID = ?1");
	if(Stmt == NULL){
		LOG_ERR("Failed to prepare query");
		return false;
	}

	if(sqlite3_bind_int(Stmt, 1, WorldID) != SQLITE_OK){
		LOG_ERR("Failed to bind WorldID: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	if(sqlite3_step(Stmt) != SQLITE_DONE){
		LOG_ERR("Failed to execute query: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	return true;
}

bool InsertHouses(int WorldID, int NumHouses, THouse *Houses){
	sqlite3_stmt *Stmt = PrepareQuery(
			"INSERT INTO Houses (WorldID, HouseID, Name, Rent, Description,"
				" Size, PositionX, PositionY, PositionZ, Town, GuildHouse)"
			" VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10, ?11)");
	if(Stmt == NULL){
		LOG_ERR("Failed to prepare query");
		return false;
	}

	if(sqlite3_bind_int(Stmt, 1, WorldID) != SQLITE_OK){
		LOG_ERR("Failed to bind WorldID: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	for(int i = 0; i < NumHouses; i += 1){
		if(sqlite3_bind_int(Stmt, 2, Houses[i].HouseID)                != SQLITE_OK
		|| sqlite3_bind_text(Stmt, 3, Houses[i].Name, -1, NULL)        != SQLITE_OK
		|| sqlite3_bind_int(Stmt, 4, Houses[i].Rent)                   != SQLITE_OK
		|| sqlite3_bind_text(Stmt, 5, Houses[i].Description, -1, NULL) != SQLITE_OK
		|| sqlite3_bind_int(Stmt, 6, Houses[i].Size)                   != SQLITE_OK
		|| sqlite3_bind_int(Stmt, 7, Houses[i].PositionX)              != SQLITE_OK
		|| sqlite3_bind_int(Stmt, 8, Houses[i].PositionY)              != SQLITE_OK
		|| sqlite3_bind_int(Stmt, 9, Houses[i].PositionZ)              != SQLITE_OK
		|| sqlite3_bind_text(Stmt, 10, Houses[i].Town, -1, NULL)       != SQLITE_OK
		|| sqlite3_bind_int(Stmt, 11, Houses[i].GuildHouse ? 1: 0)     != SQLITE_OK){
			LOG_ERR("Failed to bind parameters for house %d: %s",
					Houses[i].HouseID, sqlite3_errmsg(g_Database));
			return false;
		}

		if(sqlite3_step(Stmt) != SQLITE_DONE){
			LOG_ERR("Failed to insert house %d: %s",
					Houses[i].HouseID, sqlite3_errmsg(g_Database));
			return false;
		}

		sqlite3_reset(Stmt);
	}

	return true;
}

bool ClearIsOnline(int WorldID, int *NumAffectedCharacters){
	sqlite3_stmt *Stmt = PrepareQuery(
			"UPDATE Characters SET IsOnline = 0"
			" WHERE WorldID = ?1 AND IsOnline != 0");
	if(Stmt == NULL){
		LOG_ERR("Failed to prepare query");
		return false;
	}

	if(sqlite3_bind_int(Stmt, 1, WorldID) != SQLITE_OK){
		LOG_ERR("Failed to bind WorldID: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	if(sqlite3_step(Stmt) != SQLITE_DONE){
		LOG_ERR("Failed to execute query: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	*NumAffectedCharacters = sqlite3_changes(g_Database);
	return true;
}

bool DeleteOnlineCharacters(int WorldID){
	sqlite3_stmt *Stmt = PrepareQuery(
			"DELETE FROM OnlineCharacters WHERE WorldID = ?1");
	if(Stmt == NULL){
		LOG_ERR("Failed to prepare query");
		return false;
	}

	if(sqlite3_bind_int(Stmt, 1, WorldID) != SQLITE_OK){
		LOG_ERR("Failed to bind WorldID: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	if(sqlite3_step(Stmt) != SQLITE_DONE){
		LOG_ERR("Failed to execute query: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	return true;
}

bool InsertOnlineCharacters(int WorldID, int NumCharacters, TOnlineCharacter *Characters){
	sqlite3_stmt *Stmt = PrepareQuery(
			"INSERT INTO OnlineCharacters (WorldID, Name, Level, Profession)"
			" VALUES (?1, ?2, ?3, ?4)");
	if(Stmt == NULL){
		LOG_ERR("Failed to prepare query");
		return false;
	}

	if(sqlite3_bind_int(Stmt, 1, WorldID) != SQLITE_OK){
		LOG_ERR("Failed to bind WorldID: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	for(int i = 0; i < NumCharacters; i += 1){
		if(sqlite3_bind_text(Stmt, 2, Characters[i].Name, -1, NULL)       != SQLITE_OK
		|| sqlite3_bind_int(Stmt, 3, Characters[i].Level)                 != SQLITE_OK
		|| sqlite3_bind_text(Stmt, 4, Characters[i].Profession, -1, NULL) != SQLITE_OK){
			LOG_ERR("Failed to bind parameters for character \"%s\": %s",
					Characters[i].Name, sqlite3_errmsg(g_Database));
			return false;
		}

		if(sqlite3_step(Stmt) != SQLITE_DONE){
			LOG_ERR("Failed to insert character \"%s\": %s",
					Characters[i].Name, sqlite3_errmsg(g_Database));
			return false;
		}

		sqlite3_reset(Stmt);
	}

	return true;
}

bool CheckOnlineRecord(int WorldID, int NumCharacters, bool *NewRecord){
	ASSERT(NewRecord);
	sqlite3_stmt *Stmt = PrepareQuery(
			"UPDATE Worlds SET OnlineRecord = ?2"
			" WHERE WorldID = ?1 AND OnlineRecord < ?2");
	if(Stmt == NULL){
		LOG_ERR("Failed to prepare query");
		return false;
	}

	if(sqlite3_bind_int(Stmt, 1, WorldID) != SQLITE_OK){
		LOG_ERR("Failed to bind WorldID: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	if(sqlite3_bind_int(Stmt, 2, NumCharacters) != SQLITE_OK){
		LOG_ERR("Failed to bind NumCharacters: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	if(sqlite3_step(Stmt) != SQLITE_DONE){
		LOG_ERR("Failed to execute query: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	*NewRecord = sqlite3_changes(g_Database) > 0;
	return true;
}

bool LoadCharacterIndex(int WorldID, int MinimumCharacterID,
		int MaxEntries, int *NumEntries, TCharacterIndexEntry *Entries){
	ASSERT(MaxEntries > 0 && NumEntries && Entries);
	sqlite3_stmt *Stmt = PrepareQuery(
			"SELECT CharacterID, Name FROM Characters"
			" WHERE WorldID = ?1 AND CharacterID >= ?2"
			" ORDER BY CharacterID ASC LIMIT ?3");
	if(Stmt == NULL){
		LOG_ERR("Failed to prepare query");
		return false;
	}

	if(sqlite3_bind_int(Stmt, 1, WorldID) != SQLITE_OK){
		LOG_ERR("Failed to bind WorldID: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	if(sqlite3_bind_int(Stmt, 2, MinimumCharacterID) != SQLITE_OK){
		LOG_ERR("Failed to bind MinimumCharacterID: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	if(sqlite3_bind_int(Stmt, 3, MaxEntries) != SQLITE_OK){
		LOG_ERR("Failed to bind MaxEntries: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	// NOTE(fusion): We shouldn't get more than `MaxEntries` rows but it's
	// always better to be safe.
	int EntryIndex = 0;
	while(sqlite3_step(Stmt) == SQLITE_ROW && EntryIndex < MaxEntries){
		Entries[EntryIndex].CharacterID = sqlite3_column_int(Stmt, 0);
		StringCopy(Entries[EntryIndex].Name,
				sizeof(Entries[EntryIndex].Name),
				(const char *)sqlite3_column_text(Stmt, 1));
	}

	if(sqlite3_errcode(g_Database) != SQLITE_DONE){
		LOG_ERR("Failed to execute query: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	*NumEntries = EntryIndex;
	return true;
}

bool LoadWorldConfig(int WorldID, TWorldConfig *WorldConfig){
	ASSERT(WorldConfig);
	sqlite3_stmt *Stmt = PrepareQuery(
			"SELECT Type, RebootTime, Address, Port, MaxPlayers,"
				" PremiumPlayerBuffer, MaxNewbies, PremiumNewbieBuffer"
			" FROM Worlds WHERE WorldID = ?1");
	if(Stmt == NULL){
		LOG_ERR("Failed to prepare query");
		return false;
	}

	if(sqlite3_bind_int(Stmt, 1, WorldID) != SQLITE_OK){
		LOG_ERR("Failed to bind WorldID: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	if(sqlite3_step(Stmt) != SQLITE_ROW){
		LOG_ERR("Failed to execute query: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	WorldConfig->Type					= sqlite3_column_int(Stmt, 0);
	WorldConfig->RebootTime				= sqlite3_column_int(Stmt, 1);
	WorldConfig->Address				= sqlite3_column_int(Stmt, 2);
	WorldConfig->Port					= sqlite3_column_int(Stmt, 3);
	WorldConfig->MaxPlayers				= sqlite3_column_int(Stmt, 4);
	WorldConfig->PremiumPlayerBuffer	= sqlite3_column_int(Stmt, 5);
	WorldConfig->MaxNewbies				= sqlite3_column_int(Stmt, 6);
	WorldConfig->PremiumNewbieBuffer	= sqlite3_column_int(Stmt, 7);
	return true;
}

// Database Initialization
//==============================================================================
// NOTE(fusion): From `https://www.sqlite.org/pragma.html`:
//	"Some pragmas take effect during the SQL compilation stage, not the execution
// stage. This means if using the C-language sqlite3_prepare(), sqlite3_step(),
// sqlite3_finalize() API (or similar in a wrapper interface), the pragma may run
// during the sqlite3_prepare() call, not during the sqlite3_step() call as normal
// SQL statements do. Or the pragma might run during sqlite3_step() just like normal
// SQL statements. Whether or not the pragma runs during sqlite3_prepare() or
// sqlite3_step() depends on the pragma and on the specific release of SQLite."
//
//	Depending on the pragma, queries will fail in the sqlite3_prepare() stage when
// using bound parameters. This means we need to assemble the entire query before
// hand with snprintf or other similar formatting functions. In particular, this
// rule apply for `application_id` and `user_version` which we modify.

bool FileExists(const char *FileName){
	FILE *File = fopen(FileName, "rb");
	bool Result = (File != NULL);
	if(Result){
		fclose(File);
	}
	return Result;
}

bool ExecFile(const char *FileName){
	FILE *File = fopen(FileName, "rb");
	if(File == NULL){
		LOG_ERR("Failed to open file \"%s\"", FileName);
		return false;
	}

	fseek(File, 0, SEEK_END);
	usize FileSize = (usize)ftell(File);
	fseek(File, 0, SEEK_SET);

	bool Result = true;
	if(FileSize > 0){
		char *Text = (char*)malloc(FileSize + 1);
		Text[FileSize] = 0;
		if(Result && fread(Text, 1, FileSize, File) != FileSize){
			LOG_ERR("Failed to read \"%s\" (ferror: %d, feof: %d)",
					FileName, ferror(File), feof(File));
			Result = false;
		}

		if(Result && sqlite3_exec(g_Database, Text, NULL, NULL, NULL) != SQLITE_OK){
			LOG_ERR("Failed to execute \"%s\": %s",
					FileName, sqlite3_errmsg(g_Database));
			Result = false;
		}

		free(Text);
	}

	fclose(File);
	return Result;
}

bool ExecInternal(const char *Format, ...) ATTR_PRINTF(1, 2);
bool ExecInternal(const char *Format, ...){
	va_list ap;
	va_start(ap, Format);
	char Text[1024];
	int Written = vsnprintf(Text, sizeof(Text), Format, ap);
	va_end(ap);

	if(Written >= (int)sizeof(Text)){
		LOG_ERR("Query is too long");
		return false;
	}

	if(sqlite3_exec(g_Database, Text, NULL, NULL, NULL) != SQLITE_OK){
		LOG_ERR("Failed to execute query: %s", sqlite3_errmsg(g_Database));
		return false;
	}

	return true;
}

bool GetPragmaInt(const char *Name, int *OutValue){
	char Text[1024];
	snprintf(Text, sizeof(Text), "PRAGMA %s", Name);

	sqlite3_stmt *Stmt;
	if(sqlite3_prepare_v2(g_Database, Text, -1, &Stmt, NULL) != SQLITE_OK){
		LOG_ERR("Failed to retrieve %s (PREP): %s", Name, sqlite3_errmsg(g_Database));
		return false;
	}

	bool Result = (sqlite3_step(Stmt) == SQLITE_ROW);
	if(!Result){
		LOG_ERR("Failed to retrieve %s (PREP): %s", Name, sqlite3_errmsg(g_Database));
	}else if(OutValue){
		*OutValue = sqlite3_column_int(Stmt, 0);
	}

	sqlite3_finalize(Stmt);
	return Result;
}

bool InitDatabaseSchema(void){
	bool Result = true;

	if(Result && !ExecInternal("BEGIN")){
		LOG_ERR("Failed to start schema transaction");
		Result = false;
	}

	if(Result && !ExecFile("sql/schema.sql")){
		LOG_ERR("Failed to execute \"sql/schema.sql\"");
		Result = false;
	}

	if(Result && !ExecInternal("PRAGMA application_id = %d", g_ApplicationID)){
		LOG_ERR("Failed to set application id");
		Result = false;
	}

	if(Result && !ExecInternal("PRAGMA user_version = 1")){
		LOG_ERR("Failed to set user version");
		Result = false;
	}

	if(Result && !ExecInternal("COMMIT")){
		LOG_ERR("Failed to commit schema transaction");
		Result = false;
	}

	if(!Result && !ExecInternal("ROLLBACK")){
		LOG_ERR("Failed to rollback schema transaction");
	}

	return Result;
}

bool UpgradeDatabaseSchema(int UserVersion){
	char FileName[256];
	int NewVersion = UserVersion;
	while(true){
		snprintf(FileName, sizeof(FileName), "sql/upgrade-%d.sql", NewVersion);
		if(FileExists(FileName)){
			NewVersion += 1;
		}else{
			break;
		}
	}

	bool Result = true;
	if(UserVersion != NewVersion){
		LOG("Upgrading database schema to version %d", NewVersion);

		if(Result && !ExecInternal("BEGIN")){
			LOG_ERR("Failed to start upgrade transaction");
			Result = false;
		}

		while(Result && UserVersion < NewVersion){
			snprintf(FileName, sizeof(FileName), "upgrade-%d.sql", UserVersion);
			if(!ExecFile(FileName)){
				LOG_ERR("Failed to execute \"%s\"", FileName);
				Result = false;
			}
			UserVersion += 1;
		}

		if(Result && !ExecInternal("PRAGMA user_version = %d", UserVersion)){
			LOG_ERR("Failed to set user version");
			Result = false;
		}

		if(Result && !ExecInternal("COMMIT")){
			LOG_ERR("Failed to commit upgrade transaction");
			Result = false;
		}

		if(!Result && !ExecInternal("ROLLBACK")){
			LOG_ERR("Failed to rollback upgrade transaction");
		}
	}

	return Result;
}

bool CheckDatabaseSchema(void){
	int ApplicationID, UserVersion;
	if(!GetPragmaInt("application_id", &ApplicationID)
	|| !GetPragmaInt("user_version", &UserVersion)){
		return false;
	}

	if(ApplicationID != g_ApplicationID){
		if(ApplicationID != 0){
			LOG_ERR("Database has unknown application id %08X (expected %08X)",
					ApplicationID, g_ApplicationID);
			return false;
		}else if(UserVersion != 0){
			LOG_ERR("Database has non zero user version %d", UserVersion);
			return false;
		}else if(!InitDatabaseSchema()){
			LOG_ERR("Failed to initialize database schema");
			return false;
		}

		UserVersion = 1;
	}

	if(!UpgradeDatabaseSchema(UserVersion)){
		LOG_ERR("Failed to upgrade database schema");
		return false;
	}

	LOG("Database version: %d", UserVersion);
	return true;
}

bool InitDatabase(void){
	LOG("Database file: \"%s\"", g_DatabaseFile);
	LOG("Max cached statements: %d", g_MaxCachedStatements);

	int Flags = SQLITE_OPEN_READWRITE
			| SQLITE_OPEN_CREATE
			| SQLITE_OPEN_NOMUTEX;
	if(sqlite3_open_v2(g_DatabaseFile, &g_Database, Flags, NULL) != SQLITE_OK){
		LOG_ERR("Failed to open database at \"%s\": %s\n",
				g_DatabaseFile, sqlite3_errmsg(g_Database));
		return false;
	}

	if(!InitStatementCache()){
		LOG_ERR("Failed to initialize statement cache");
		return false;
	}

	if(!CheckDatabaseSchema()){
		LOG_ERR("Failed to check database schema");
		return false;
	}

	return true;
}

void ExitDatabase(void){
	ExitStatementCache();

	if(g_Database != NULL){
		// NOTE(fusion): `sqlite3_close` can only fail if there are associated
		// prepared statements, blob handles, or backup objects that were not
		// finalized.
		if(sqlite3_close(g_Database) == SQLITE_OK){
			g_Database = NULL;
		}else{
			LOG_ERR("Failed to close database: %s", sqlite3_errmsg(g_Database));
		}
	}
}
