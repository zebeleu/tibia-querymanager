-- Primary Tables
--==============================================================================
CREATE TABLE IF NOT EXISTS Worlds (
	WorldID INTEGER NOT NULL,
	Name TEXT NOT NULL COLLATE NOCASE,
	Type INTEGER NOT NULL,
	RebootTime INTEGER NOT NULL,
	Host TEXT NOT NULL,
	Port INTEGER NOT NULL,
	MaxPlayers INTEGER NOT NULL,
	PremiumPlayerBuffer INTEGER NOT NULL,
	MaxNewbies INTEGER NOT NULL,
	PremiumNewbieBuffer INTEGER NOT NULL,
	OnlineRecord INTEGER NOT NULL DEFAULT 0,
	OnlineRecordTimestamp INTEGER NOT NULL DEFAULT 0,
	PRIMARY KEY (WorldID),
	UNIQUE (Name)
);

CREATE TABLE IF NOT EXISTS Accounts (
	AccountID INTEGER NOT NULL,
	Email TEXT NOT NULL COLLATE NOCASE,
	Auth BLOB NOT NULL,
	PremiumEnd INTEGER NOT NULL DEFAULT 0,
	PendingPremiumDays INTEGER NOT NULL DEFAULT 0,
	Deleted INTEGER NOT NULL DEFAULT 0,
	PRIMARY KEY (AccountID),
	UNIQUE (Email)
);

CREATE TABLE IF NOT EXISTS Characters (
	WorldID INTEGER NOT NULL,
	CharacterID INTEGER NOT NULL,
	AccountID INTEGER NOT NULL,
	Name TEXT NOT NULL COLLATE NOCASE,
	Sex INTEGER NOT NULL,
	Guild TEXT NOT NULL COLLATE NOCASE DEFAULT '',
	Rank TEXT NOT NULL COLLATE NOCASE DEFAULT '',
	Title TEXT NOT NULL COLLATE NOCASE DEFAULT '',
	Level INTEGER NOT NULL DEFAULT 0,
	Profession TEXT NOT NULL DEFAULT '',
	Residence TEXT NOT NULL DEFAULT '',
	LastLoginTime INTEGER NOT NULL DEFAULT 0,
	TutorActivities INTEGER NOT NULL DEFAULT 0,
	IsOnline INTEGER NOT NULL DEFAULT 0,
	Deleted INTEGER NOT NULL DEFAULT 0,
	PRIMARY KEY (CharacterID),
	UNIQUE (Name)
);
CREATE INDEX IF NOT EXISTS CharactersWorldIndex
		ON Characters(WorldID, IsOnline);
CREATE INDEX IF NOT EXISTS CharactersAccountIndex
		ON Characters(AccountID, IsOnline);
CREATE INDEX IF NOT EXISTS CharactersGuildIndex
		ON Characters(Guild, Rank);

CREATE TABLE IF NOT EXISTS CharacterRights (
	CharacterID INTEGER NOT NULL,
	Right TEXT NOT NULL COLLATE NOCASE,
	PRIMARY KEY(CharacterID, Right)
);

CREATE TABLE IF NOT EXISTS CharacterDeaths (
	CharacterID INTEGER NOT NULL,
	Level INTEGER NOT NULL,
	OffenderID INTEGER NOT NULL,
	Remark TEXT NOT NULL,
	Unjustified INTEGER NOT NULL,
	Timestamp INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS CharacterDeathsCharacterIndex
		ON CharacterDeaths(CharacterID, Level);
CREATE INDEX IF NOT EXISTS CharacterDeathsOffenderIndex
		ON CharacterDeaths(OffenderID, Unjustified);

CREATE TABLE IF NOT EXISTS Buddies (
	WorldID INTEGER NOT NULL,
	AccountID INTEGER NOT NULL,
	BuddyID INTEGER NOT NULL,
	PRIMARY KEY (WorldID, AccountID, BuddyID)
);

CREATE TABLE IF NOT EXISTS WorldInvitations (
	WorldID INTEGER NOT NULL,
	CharacterID INTEGER NOT NULL,
	PRIMARY KEY (WorldID, CharacterID)
);

CREATE TABLE IF NOT EXISTS LoginAttempts (
	AccountID INTEGER NOT NULL,
	IPAddress INTEGER NOT NULL,
	Timestamp INTEGER NOT NULL,
	Failed INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS LoginAttemptsAccountIndex
		ON LoginAttempts(AccountID, Timestamp);
CREATE INDEX IF NOT EXISTS LoginAttemptsAddressIndex
		ON LoginAttempts(IPAddress, Timestamp);

-- House Tables
--==============================================================================
CREATE TABLE IF NOT EXISTS Houses (
	WorldID INTEGER NOT NULL,
	HouseID INTEGER NOT NULL,
	Name TEXT NOT NULL,
	Rent INTEGER NOT NULL,
	Description TEXT NOT NULL,
	Size INTEGER NOT NULL,
	PositionX INTEGER NOT NULL,
	PositionY INTEGER NOT NULL,
	PositionZ INTEGER NOT NULL,
	Town TEXT NOT NULL,
	GuildHouse INTEGER NOT NULL,
	PRIMARY KEY (WorldID, HouseID)
);

CREATE TABLE IF NOT EXISTS HouseOwners (
	WorldID INTEGER NOT NULL,
	HouseID INTEGER NOT NULL,
	OwnerID INTEGER NOT NULL,
	PaidUntil INTEGER NOT NULL,
	PRIMARY KEY (WorldID, HouseID)
);

-- NOTE(fusion): An auction would have a non null `FinishTime` but it doesn't make
-- sense to finish an auction just to restart it afterwards so it should only be
-- set after the first bid, along with `BidderID` and `BidAmount`.
CREATE TABLE IF NOT EXISTS HouseAuctions (
	WorldID INTEGER NOT NULL,
	HouseID INTEGER NOT NULL,
	BidderID INTEGER DEFAULT NULL,
	BidAmount INTEGER DEFAULT NULL,
	FinishTime INTEGER DEFAULT NULL,
	PRIMARY KEY (WorldID, HouseID)
);

CREATE TABLE IF NOT EXISTS HouseTransfers (
	WorldID INTEGER NOT NULL,
	HouseID INTEGER NOT NULL,
	NewOwnerID INTEGER NOT NULL,
	Price INTEGER NOT NULL,
	PRIMARY KEY (WorldID, HouseID)
);

CREATE TABLE IF NOT EXISTS HouseAuctionExclusions (
	CharacterID INTEGER NOT NULL,
	Issued INTEGER NOT NULL,
	Until INTEGER NOT NULL,
	BanishmentID INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS HouseAuctionExclusionsIndex
		ON HouseAuctionExclusions(CharacterID, Until);

CREATE TABLE IF NOT EXISTS HouseAssignments (
	WorldID INTEGER NOT NULL,
	HouseID INTEGER NOT NULL,
	OwnerID INTEGER NOT NULL,
	Price INTEGER NOT NULL,
	Timestamp INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS HouseAssignmentsHouseIndex
		ON HouseAssignments(WorldID, HouseID);
CREATE INDEX IF NOT EXISTS HouseAssignmentsTimeIndex
		ON HouseAssignments(WorldID, Timestamp);
CREATE INDEX IF NOT EXISTS HouseAssignmentsOwnerIndex
		ON HouseAssignments(OwnerID);

-- Banishment Tables
--==============================================================================
CREATE TABLE IF NOT EXISTS Banishments (
	BanishmentID INTEGER NOT NULL,
	AccountID INTEGER NOT NULL,
	IPAddress INTEGER NOT NULL,
	GamemasterID INTEGER NOT NULL,
	Reason TEXT NOT NULL,
	Comment TEXT NOT NULL,
	FinalWarning INTEGER NOT NULL,
	Issued INTEGER NOT NULL,
	Until INTEGER NOT NULL,
	PRIMARY KEY (BanishmentID)
);
CREATE INDEX IF NOT EXISTS BanishmentsAccountIndex
		ON Banishments(AccountID, Until, FinalWarning);

CREATE TABLE IF NOT EXISTS IPBanishments (
	CharacterID INTEGER NOT NULL,
	IPAddress INTEGER NOT NULL,
	GamemasterID INTEGER NOT NULL,
	Reason TEXT NOT NULL,
	Comment TEXT NOT NULL,
	Issued INTEGER NOT NULL,
	Until INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS IPBanishmentsAddressIndex
		ON IPBanishments(IPAddress);
CREATE INDEX IF NOT EXISTS IPBanishmentsCharacterIndex
		ON IPBanishments(CharacterID);

CREATE TABLE IF NOT EXISTS Namelocks (
	CharacterID INTEGER NOT NULL,
	IPAddress INTEGER NOT NULL,
	GamemasterID INTEGER NOT NULL,
	Reason TEXT NOT NULL,
	Comment TEXT NOT NULL,
	Attempts INTEGER NOT NULL DEFAULT 0,
	Approved INTEGER NOT NULL DEFAULT 0,
	PRIMARY KEY (CharacterID)
);

CREATE TABLE IF NOT EXISTS Notations (
	CharacterID INTEGER NOT NULL,
	IPAddress INTEGER NOT NULL,
	GamemasterID INTEGER NOT NULL,
	Reason TEXT NOT NULL,
	Comment TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS NotationsCharacterIndex
		ON Notations(CharacterID);

CREATE TABLE IF NOT EXISTS Statements (
	WorldID INTEGER NOT NULL,
	Timestamp INTEGER NOT NULL,
	StatementID INTEGER NOT NULL,
	CharacterID INTEGER NOT NULL,
	Channel TEXT NOT NULL,
	Text TEXT NOT NULL,
	PRIMARY KEY (WorldID, Timestamp, StatementID)
);
CREATE INDEX IF NOT EXISTS StatementsCharacterIndex
		ON Statements(CharacterID, Timestamp);

CREATE TABLE IF NOT EXISTS ReportedStatements (
	WorldID INTEGER NOT NULL,
	Timestamp INTEGER NOT NULL,
	StatementID INTEGER NOT NULL,
	CharacterID INTEGER NOT NULL,
	BanishmentID INTEGER NOT NULL,
	ReporterID INTEGER NOT NULL,
	Reason TEXT NOT NULL,
	Comment TEXT NOT NULL,
	PRIMARY KEY (WorldID, Timestamp, StatementID)
);
CREATE INDEX IF NOT EXISTS ReportedStatementsCharacterIndex
		ON ReportedStatements(CharacterID, Timestamp);
CREATE INDEX IF NOT EXISTS ReportedStatementsBanishmentIndex
		ON ReportedStatements(BanishmentID);

-- Info Tables
--==============================================================================
CREATE TABLE IF NOT EXISTS KillStatistics (
	WorldID INTEGER NOT NULL,
	RaceName TEXT NOT NULL COLLATE NOCASE,
	TimesKilled INTEGER NOT NULL,
	PlayersKilled INTEGER NOT NULL,
	PRIMARY KEY (WorldID, RaceName)
);

CREATE TABLE IF NOT EXISTS OnlineCharacters (
	WorldID INTEGER NOT NULL,
	Name TEXT NOT NULL COLLATE NOCASE,
	Level INTEGER NOT NULL,
	Profession TEXT NOT NULL,
	PRIMARY KEY (WorldID, Name)
);

-- REMOVE(fusion): Testing Data.
--==============================================================================
INSERT INTO Worlds (WorldID, Name, Type, RebootTime, Host, Port, MaxPlayers,
					PremiumPlayerBuffer, MaxNewbies, PremiumNewbieBuffer)
	VALUES (1, 'Zanera', 0, 5, 'localhost', 7172, 1000, 100, 300, 100);

-- 111111/tibia
INSERT INTO Accounts (AccountID, Email, Auth)
	VALUES (111111, '@tibia', X'206699cbc2fae1683118c873d746aa376049cb5923ef0980298bb7acbba527ec9e765668f7a338dffea34acf61a20efb654c1e9c62d35148dba2aeeef8dc7788');

INSERT INTO Characters (WorldID, CharacterID, AccountID, Name, Sex)
	VALUES (1, 1, 111111, 'Gamemaster', 1), (1, 2, 111111, 'Player', 1);

/*
-- TODO(fusion): Have group rights instead of adding individual rights to characters?
ALTER TABLE Characters ADD GroupID INTEGER NOT NULL;
CREATE TABLE IF NOT EXISTS CharacterRights (
	GroupID INTEGER NOT NULL,
	Right TEXT NOT NULL COLLATE NOCASE,
	PRIMARY KEY(GroupID, Right)
);
*/

INSERT INTO CharacterRights (CharacterID, Right)
	VALUES
		(1, 'NOTATION'),
		(1, 'NAMELOCK'),
		(1, 'STATEMENT_REPORT'),
		(1, 'BANISHMENT'),
		(1, 'FINAL_WARNING'),
		(1, 'IP_BANISHMENT'),
		(1, 'KICK'),
		(1, 'HOME_TELEPORT'),
		(1, 'GAMEMASTER_BROADCAST'),
		(1, 'ANONYMOUS_BROADCAST'),
		(1, 'NO_BANISHMENT'),
		(1, 'ALLOW_MULTICLIENT'),
		(1, 'LOG_COMMUNICATION'),
		(1, 'READ_GAMEMASTER_CHANNEL'),
		(1, 'READ_TUTOR_CHANNEL'),
		(1, 'HIGHLIGHT_HELP_CHANNEL'),
		(1, 'SEND_BUGREPORTS'),
		(1, 'NAME_INSULTING'),
		(1, 'NAME_SENTENCE'),
		(1, 'NAME_NONSENSICAL_LETTERS'),
		(1, 'NAME_BADLY_FORMATTED'),
		(1, 'NAME_NO_PERSON'),
		(1, 'NAME_CELEBRITY'),
		(1, 'NAME_COUNTRY'),
		(1, 'NAME_FAKE_IDENTITY'),
		(1, 'NAME_FAKE_POSITION'),
		(1, 'STATEMENT_INSULTING'),
		(1, 'STATEMENT_SPAMMING'),
		(1, 'STATEMENT_ADVERT_OFFTOPIC'),
		(1, 'STATEMENT_ADVERT_MONEY'),
		(1, 'STATEMENT_NON_ENGLISH'),
		(1, 'STATEMENT_CHANNEL_OFFTOPIC'),
		(1, 'STATEMENT_VIOLATION_INCITING'),
		(1, 'CHEATING_BUG_ABUSE'),
		(1, 'CHEATING_GAME_WEAKNESS'),
		(1, 'CHEATING_MACRO_USE'),
		(1, 'CHEATING_MODIFIED_CLIENT'),
		(1, 'CHEATING_HACKING'),
		(1, 'CHEATING_MULTI_CLIENT'),
		(1, 'CHEATING_ACCOUNT_TRADING'),
		(1, 'CHEATING_ACCOUNT_SHARING'),
		(1, 'GAMEMASTER_THREATENING'),
		(1, 'GAMEMASTER_PRETENDING'),
		(1, 'GAMEMASTER_INFLUENCE'),
		(1, 'GAMEMASTER_FALSE_REPORTS'),
		(1, 'KILLING_EXCESSIVE_UNJUSTIFIED'),
		(1, 'DESTRUCTIVE_BEHAVIOUR'),
		(1, 'SPOILING_AUCTION'),
		(1, 'INVALID_PAYMENT'),
		(1, 'TELEPORT_TO_CHARACTER'),
		(1, 'TELEPORT_TO_MARK'),
		(1, 'TELEPORT_VERTICAL'),
		(1, 'TELEPORT_TO_COORDINATE'),
		(1, 'LEVITATE'),
		(1, 'SPECIAL_MOVEUSE'),
		(1, 'MODIFY_GOSTRENGTH'),
		(1, 'SHOW_COORDINATE'),
		(1, 'RETRIEVE'),
		(1, 'ENTER_HOUSES'),
		(1, 'OPEN_NAMEDOORS'),
		(1, 'INVULNERABLE'),
		(1, 'UNLIMITED_MANA'),
		(1, 'KEEP_INVENTORY'),
		(1, 'ALL_SPELLS'),
		(1, 'UNLIMITED_CAPACITY'),
		(1, 'ATTACK_EVERYWHERE'),
		(1, 'NO_LOGOUT_BLOCK'),
		(1, 'GAMEMASTER_OUTFIT'),
		(1, 'ILLUMINATE'),
		(1, 'CHANGE_PROFESSION'),
		(1, 'IGNORED_BY_MONSTERS'),
		(1, 'SHOW_KEYHOLE_NUMBERS'),
		(1, 'CREATE_OBJECTS'),
		(1, 'CREATE_MONEY'),
		(1, 'CREATE_MONSTERS'),
		(1, 'CHANGE_SKILLS'),
		(1, 'CLEANUP_FIELDS'),
		(1, 'NO_STATISTICS');
