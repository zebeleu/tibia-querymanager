#include "querymanager.hh"

// TODO(fusion): Eventually support Windows? It's not that difficult to wrap the
// few OS specific calls but it can be annoying plus the socket type is slightly
// different.
#if OS_LINUX
#	include <errno.h>
#	include <fcntl.h>
#	include <netinet/in.h>
#	include <poll.h>
#	include <sys/socket.h>
#	include <unistd.h>
#	include <time.h>
#else
#	error "Operating system not currently supported."
#endif

static int g_Listener = -1;
static TConnection *g_Connections;

// Connection Handling
//==============================================================================
int ListenerBind(uint16 Port){
	int Socket = socket(AF_INET, SOCK_STREAM, 0);
	if(Socket == -1){
		LOG_ERR("Failed to create listener socket: (%d) %s", errno, strerrordesc_np(errno));
		return -1;
	}

	int ReuseAddr = 1;
	if(setsockopt(Socket, SOL_SOCKET, SO_REUSEADDR, &ReuseAddr, sizeof(ReuseAddr)) == -1){
		LOG_ERR("Failed to set SO_REUSADDR: (%d) %s", errno, strerrordesc_np(errno));
		close(Socket);
		return -1;
	}

	int Flags = fcntl(Socket, F_GETFL);
	if(Flags == -1){
		LOG_ERR("Failed to get socket flags: (%d) %s", errno, strerrordesc_np(errno));
		close(Socket);
		return -1;
	}

	if(fcntl(Socket, F_SETFL, Flags | O_NONBLOCK) == -1){
		LOG_ERR("Failed to set socket flags: (%d) %s", errno, strerrordesc_np(errno));
		close(Socket);
		return -1;
	}

	// IMPORTANT(fusion): Binding the socket to the LOOPBACK address should allow
	// only local connections to be accepted. This is VERY important as the protocol
	// IS NOT encrypted.
	sockaddr_in Addr = {};
	Addr.sin_family = AF_INET;
	Addr.sin_port = htons(Port);
	Addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if(bind(Socket, (sockaddr*)&Addr, sizeof(Addr)) == -1){
		LOG_ERR("Failed to bind socket to port %d: (%d) %s", Port, errno, strerrordesc_np(errno));
		close(Socket);
		return -1;
	}

	if(listen(Socket, 128) == -1){
		LOG_ERR("Failed to listen to port %d: (%d) %s", Port, errno, strerrordesc_np(errno));
		close(Socket);
		return -1;
	}

	return Socket;
}

int ListenerAccept(int Listener, uint32 *OutAddr, uint16 *OutPort){
	while(true){
		sockaddr_in SocketAddr = {};
		socklen_t SocketAddrLen = sizeof(SocketAddr);
		int Socket = accept(Listener, (sockaddr*)&SocketAddr, &SocketAddrLen);
		if(Socket == -1){
			if(errno != EAGAIN){
				LOG_ERR("Failed to accept connection: (%d) %s", errno, strerrordesc_np(errno));
			}
			return -1;
		}

		// IMPORTANT(fusion): It should be impossible to spoof the loopback
		// address so this comparison should be safe. We're also binding the
		// listening socket to the loopback address which should prevent any
		// other address to show up here.
		uint32 Addr = ntohl(SocketAddr.sin_addr.s_addr);
		uint16 Port = ntohs(SocketAddr.sin_port);
		if(Addr != INADDR_LOOPBACK){
			LOG_ERR("Rejecting remote connection from %08X.", Addr);
			close(Socket);
			continue;
		}

		int Flags = fcntl(Socket, F_GETFL);
		if(Flags == -1){
			LOG_ERR("Failed to get socket flags: (%d) %s", errno, strerrordesc_np(errno));
			close(Socket);
			continue;
		}

		if(fcntl(Socket, F_SETFL, Flags | O_NONBLOCK) == -1){
			LOG_ERR("Failed to set socket flags: (%d) %s", errno, strerrordesc_np(errno));
			close(Socket);
			continue;
		}

		if(OutAddr){
			*OutAddr = Addr;
		}

		if(OutPort){
			*OutPort = Port;
		}

		return Socket;
	}
}

void CloseConnection(TConnection *Connection){
	if(Connection->Socket != -1){
		close(Connection->Socket);
		Connection->Socket = -1;
	}
}

void EnsureConnectionBuffer(TConnection *Connection){
	if(Connection->Buffer == NULL){
		Connection->Buffer = (uint8*)malloc(g_MaxConnectionPacketSize);
	}
}

void DeleteConnectionBuffer(TConnection *Connection){
	if(Connection->Buffer != NULL){
		free(Connection->Buffer);
		Connection->Buffer = NULL;
	}
}

TConnection *AssignConnection(int Socket, uint32 Addr, uint16 Port){
	int ConnectionIndex = -1;
	for(int i = 0; i < g_MaxConnections; i += 1){
		if(g_Connections[i].State == CONNECTION_FREE){
			ConnectionIndex = i;
			break;
		}
	}

	TConnection *Connection = NULL;
	if(ConnectionIndex != -1){
		Connection = &g_Connections[ConnectionIndex];
		Connection->State = CONNECTION_READING;
		Connection->Socket = Socket;
		Connection->LastActive = g_MonotonicTimeMS;
		snprintf(Connection->RemoteAddress,
				sizeof(Connection->RemoteAddress),
				"%d.%d.%d.%d:%d",
				((int)(Addr >> 24) & 0xFF),
				((int)(Addr >> 16) & 0xFF),
				((int)(Addr >>  8) & 0xFF),
				((int)(Addr >>  0) & 0xFF),
				(int)Port);

		LOG("Connection %s assigned to slot %d",
				Connection->RemoteAddress, ConnectionIndex);
	}
	return Connection;
}

void ReleaseConnection(TConnection *Connection){
	if(Connection->State != CONNECTION_FREE){
		LOG("Connection %s released", Connection->RemoteAddress);
		CloseConnection(Connection);
		DeleteConnectionBuffer(Connection);
		memset(Connection, 0, sizeof(TConnection));
		Connection->State = CONNECTION_FREE;
		Connection->Socket = -1;
	}
}

void CheckConnectionInput(TConnection *Connection, int Events){
	if((Events & POLLIN) == 0 || Connection->Socket == -1){
		return;
	}

	if(Connection->State != CONNECTION_READING){
		LOG_ERR("Connection %s (State: %d) sending out-of-order data",
				Connection->RemoteAddress, Connection->State);
		CloseConnection(Connection);
		return;
	}

	EnsureConnectionBuffer(Connection);
	while(true){
		int ReadSize = Connection->RWSize;
		if(ReadSize == 0){
			if(Connection->RWPosition < 2){
				ReadSize = 2 - Connection->RWPosition;
			}else{
				ReadSize = 6 - Connection->RWPosition;
			}
			ASSERT(ReadSize > 0);
		}

		int BytesRead = read(Connection->Socket,
				(Connection->Buffer + Connection->RWPosition),
				(ReadSize           - Connection->RWPosition));
		if(BytesRead == -1){
			if(errno != EAGAIN){
				// NOTE(fusion): Connection error.
				CloseConnection(Connection);
			}
			break;
		}else if(BytesRead == 0){
			// NOTE(fusion): Graceful close.
			CloseConnection(Connection);
			break;
		}

		Connection->RWPosition += BytesRead;
		if(Connection->RWPosition >= ReadSize){
			if(Connection->RWSize != 0){
				Connection->State = CONNECTION_PROCESSING;
				Connection->LastActive = g_MonotonicTimeMS;
				break;
			}else if(Connection->RWPosition == 2){
				int PayloadSize = BufferRead16LE(Connection->Buffer);
				if(PayloadSize <= 0 || PayloadSize > g_MaxConnectionPacketSize){
					CloseConnection(Connection);
					break;
				}

				if(PayloadSize != 0xFFFF){
					Connection->RWSize = PayloadSize;
					Connection->RWPosition = 0;
				}
			}else if(Connection->RWPosition == 6){
				int PayloadSize = (int)BufferRead32LE(Connection->Buffer + 2);
				if(PayloadSize <= 0 || PayloadSize > g_MaxConnectionPacketSize){
					CloseConnection(Connection);
					break;
				}

				Connection->RWSize = PayloadSize;
				Connection->RWPosition = 0;
			}else{
				PANIC("Invalid input state (State: %d, RWSize: %d, RWPosition: %d)",
						Connection->State, Connection->RWSize, Connection->RWPosition);
			}
		}
	}

	if(Connection->State == CONNECTION_PROCESSING){
		ProcessConnectionQuery(Connection);
	}
}

void CheckConnectionOutput(TConnection *Connection, int Events){
	if((Events & POLLOUT) == 0 || Connection->Socket == -1){
		return;
	}

	if(Connection->State != CONNECTION_WRITING){
		return;
	}

	while(true){
		int BytesWritten = write(Connection->Socket,
				(Connection->Buffer + Connection->RWPosition),
				(Connection->RWSize - Connection->RWPosition));
		if(BytesWritten == -1){
			if(errno != EAGAIN){
				CloseConnection(Connection);
			}
			break;
		}

		Connection->RWPosition += BytesWritten;
		if(Connection->RWPosition >= Connection->RWSize){
			Connection->State = CONNECTION_READING;
			Connection->RWSize = 0;
			Connection->RWPosition = 0;
			break;
		}
	}
}

void CheckConnection(TConnection *Connection, int Events){
	ASSERT((Events & POLLNVAL) == 0);

	if((Events & (POLLERR | POLLHUP)) != 0){
		CloseConnection(Connection);
	}

	if(g_MaxConnectionIdleTime > 0){
		int IdleTime = (g_MonotonicTimeMS - Connection->LastActive);
		if(IdleTime >= g_MaxConnectionIdleTime){
			LOG_WARN("Dropping connection %s due to inactivity",
					Connection->RemoteAddress);
			CloseConnection(Connection);
		}
	}

	if(Connection->Socket == -1){
		ReleaseConnection(Connection);
	}
}

void ProcessConnections(void){
	// NOTE(fusion): Accept new connections.
	while(true){
		uint32 Addr;
		uint16 Port;
		int Socket = ListenerAccept(g_Listener, &Addr, &Port);
		if(Socket == -1){
			break;
		}

		if(AssignConnection(Socket, Addr, Port) == NULL){
			LOG_ERR("Rejecting connection from %08X due to max number of"
					" connections being reached (%d)", Addr, g_MaxConnections);
			close(Socket);
		}
	}

	// NOTE(fusion): Gather active connections.
	int NumConnections = 0;
	int *ConnectionIndices = (int*)alloca(g_MaxConnections * sizeof(int));
	pollfd *ConnectionFds  = (pollfd*)alloca(g_MaxConnections * sizeof(pollfd));
	for(int i = 0; i < g_MaxConnections; i += 1){
		if(g_Connections[i].State == CONNECTION_FREE || g_Connections[i].Socket == -1){
			continue;
		}

		ConnectionIndices[NumConnections] = i;
		ConnectionFds[NumConnections].fd = g_Connections[i].Socket;
		ConnectionFds[NumConnections].events = POLLIN | POLLOUT;
		ConnectionFds[NumConnections].revents = 0;
		NumConnections += 1;
	}

	if(NumConnections <= 0){
		return;
	}

	// NOTE(fusion): Poll connections.
	int NumEvents = poll(ConnectionFds, NumConnections, 0);
	if(NumEvents == -1){
		LOG_ERR("Failed to poll connections: (%d) %s", errno, strerrordesc_np(errno));
		return;
	}

	// NOTE(fusion): Process connections.
	for(int i = 0; i < NumConnections; i += 1){
		TConnection *Connection = &g_Connections[ConnectionIndices[i]];
		int Events = (int)ConnectionFds[i].revents;
		CheckConnectionInput(Connection, Events);
		CheckConnectionOutput(Connection, Events);
		CheckConnection(Connection, Events);
	}
}

bool InitConnections(void){
	ASSERT(g_Listener == -1);
	ASSERT(g_Connections == NULL);

	LOG("Listening port: %d", g_Port);
	LOG("Max connections: %d", g_MaxConnections);
	LOG("Max connection idle time: %d ms", g_MaxConnectionIdleTime);
	LOG("Max connection packet size: %d", g_MaxConnectionPacketSize);

	g_Listener = ListenerBind((uint16)g_Port);
	if(g_Listener == -1){
		LOG_ERR("Failed to bind listener");
		return false;
	}

	g_Connections = (TConnection*)calloc(
			g_MaxConnections, sizeof(TConnection));
	for(int i = 0; i < g_MaxConnections; i += 1){
		g_Connections[i].State = CONNECTION_FREE;
	}

	return true;
}

void ExitConnections(void){
	if(g_Listener != -1){
		close(g_Listener);
		g_Listener = -1;
	}

	if(g_Connections != NULL){
		for(int i = 0; i < g_MaxConnections; i += 1){
			ReleaseConnection(&g_Connections[i]);
		}

		free(g_Connections);
	}
}

// Connection Queries
//==============================================================================
TWriteBuffer PrepareResponse(TConnection *Connection, int Status){
	if(Connection->State != CONNECTION_PROCESSING){
		LOG_ERR("Connection %s is not processing query (State: %d)",
				Connection->RemoteAddress, Connection->State);
		CloseConnection(Connection);
		return TWriteBuffer(NULL, 0);
	}

	TWriteBuffer WriteBuffer(Connection->Buffer, g_MaxConnectionPacketSize);
	WriteBuffer.Write16(0);
	WriteBuffer.Write8((uint8)Status);
	return WriteBuffer;
}

void SendResponse(TConnection *Connection, TWriteBuffer *WriteBuffer){
	if(Connection->State != CONNECTION_PROCESSING){
		LOG_ERR("Connection %s is not processing query (State: %d)",
				Connection->RemoteAddress, Connection->State);
		CloseConnection(Connection);
		return;
	}

	ASSERT(WriteBuffer != NULL
		&& WriteBuffer->Buffer == Connection->Buffer
		&& WriteBuffer->Size == g_MaxConnectionPacketSize
		&& WriteBuffer->Position > 2);

	int PayloadSize = WriteBuffer->Position - 2;
	if(PayloadSize < 0xFFFF){
		WriteBuffer->Rewrite16(0, (uint16)PayloadSize);
	}else{
		WriteBuffer->Rewrite16(0, 0xFFFF);
		WriteBuffer->Insert32(2, (uint32)PayloadSize);
	}

	if(!WriteBuffer->Overflowed()){
		Connection->State = CONNECTION_WRITING;
		Connection->RWSize = WriteBuffer->Position;
		Connection->RWPosition = 0;
	}else{
		LOG_ERR("Write buffer overflowed when writing response to %s",
				Connection->RemoteAddress);
		CloseConnection(Connection);
	}
}

void SendQueryStatusOk(TConnection *Connection){
	TWriteBuffer WriteBuffer = PrepareResponse(Connection, QUERY_STATUS_OK);
	SendResponse(Connection, &WriteBuffer);
}

void SendQueryStatusError(TConnection *Connection, int ErrorCode){
	TWriteBuffer WriteBuffer = PrepareResponse(Connection, QUERY_STATUS_ERROR);
	WriteBuffer.Write8((uint8)ErrorCode);
	SendResponse(Connection, &WriteBuffer);
}

void SendQueryStatusFailed(TConnection *Connection){
	TWriteBuffer WriteBuffer = PrepareResponse(Connection, QUERY_STATUS_FAILED);
	SendResponse(Connection, &WriteBuffer);
}

void ProcessLoginQuery(TConnection *Connection, TReadBuffer *Buffer){
	char Password[30];
	char ApplicationData[30];
	int ApplicationType = Buffer->Read8();
	Buffer->ReadString(Password, sizeof(Password));
	if(ApplicationType == APPLICATION_TYPE_GAME){
		Buffer->ReadString(ApplicationData, sizeof(ApplicationData));
	}

	if(!StringEq(g_Password, Password)){
		LOG_WARN("Invalid login attempt from %s", Connection->RemoteAddress);
		SendQueryStatusFailed(Connection);
		return;
	}

	LOG("Connection %s AUTHORIZED", Connection->RemoteAddress);
	Connection->Authorized = true;
	Connection->ApplicationType = ApplicationType;
	StringCopy(Connection->ApplicationData,
			sizeof(Connection->ApplicationData),
			ApplicationData);
	SendQueryStatusOk(Connection);
}

void ProcessCheckAccountPasswordQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessLoginAdminQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessLoginGameQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessLogoutGameQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessSetNamelockQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessBanishAccountQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessSetNotationQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessReportStatementQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessBanishIpAddressQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessLogCharacterDeathQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessAddBuddyQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessRemoveBuddyQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessDecrementIsOnlineQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessFinishAuctionsQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessTransferHousesQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessEvictFreeAccountsQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessEvictDeletedCharactersQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessEvictExGuildleadersQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessInsertHouseOwnerQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessUpdateHouseOwnerQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessDeleteHouseOwnerQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessGetHouseOwnersQuery(TConnection *Connection, TReadBuffer *Buffer){
	if(Connection->ApplicationType != APPLICATION_TYPE_GAME){
		SendQueryStatusFailed(Connection);
		return;
	}

	DynamicArray<THouseOwner> HouseOwners;
	const char *WorldName = Connection->ApplicationData;
	if(!LoadHouseOwners(WorldName, &HouseOwners)){
		SendQueryStatusFailed(Connection);
		return;
	}

	TWriteBuffer WriteBuffer = PrepareResponse(Connection, QUERY_STATUS_OK);
	int NumHouseOwners = std::min<int>(HouseOwners.Length(), UINT16_MAX);
	WriteBuffer.Write16((uint16)NumHouseOwners);
	for(int i = 0; i < NumHouseOwners; i += 1){
		WriteBuffer.Write16((uint16)HouseOwners[i].HouseID);
		WriteBuffer.Write32((uint32)HouseOwners[i].OwnerID);
		WriteBuffer.WriteString(HouseOwners[i].OwnerName);
		WriteBuffer.Write32((uint32)HouseOwners[i].PaidUntil);
	}
	SendResponse(Connection, &WriteBuffer);
}

void ProcessGetAuctionsQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessStartAuctionQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessInsertHousesQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessClearIsOnlineQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessCreatePlayerlistQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessLogKilledCreaturesQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessLoadPlayersQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessExcludeFromAuctionsQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessCancelHouseTransferQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessLoadWorldConfigQuery(TConnection *Connection, TReadBuffer *Buffer){
	if(Connection->ApplicationType != APPLICATION_TYPE_GAME){
		SendQueryStatusFailed(Connection);
		return;
	}

	TWorldConfig WorldConfig = {};
	const char *WorldName = Connection->ApplicationData;
	if(!LoadWorldConfig(WorldName, &WorldConfig)){
		SendQueryStatusFailed(Connection);
		return;
	}

	TWriteBuffer WriteBuffer = PrepareResponse(Connection, QUERY_STATUS_OK);
	WriteBuffer.Write8((uint8)WorldConfig.Type);
	WriteBuffer.Write8((uint8)WorldConfig.RebootTime);
	WriteBuffer.Write32BE((uint32)WorldConfig.Address);
	WriteBuffer.Write16((uint16)WorldConfig.Port);
	WriteBuffer.Write16((uint16)WorldConfig.MaxPlayers);
	WriteBuffer.Write16((uint16)WorldConfig.PremiumPlayerBuffer);
	WriteBuffer.Write16((uint16)WorldConfig.MaxNewbies);
	WriteBuffer.Write16((uint16)WorldConfig.PremiumNewbieBuffer);
	SendResponse(Connection, &WriteBuffer);
}

void ProcessGetKeptCharactersQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessGetDeletedCharactersQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessDeleteOldCharacterQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessGetHiddenCharactersQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessCreateHighscoresQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessCreateCensusQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessCreateKillStatisticsQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessGetPlayersOnlineQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessGetWorldsQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessGetServerLoadQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessInsertPaymentDataOldQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessAddPaymentOldQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessCancelPaymentOldQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessInsertPaymentDataNewQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessAddPaymentNewQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessCancelPaymentNewQuery(TConnection *Connection, TReadBuffer *Buffer){
	SendQueryStatusFailed(Connection);
}

void ProcessConnectionQuery(TConnection *Connection){
	// TODO(fusion): Ideally we'd create a new query and dispatch it to the
	// database thread for processing. Realistically, it wouldn't make a big
	// difference since we're already handling connections asynchronously and
	// the only blocking system calls are done by SQLite when interacting with
	// the disk.

	TReadBuffer Buffer(Connection->Buffer, Connection->RWSize);
	uint8 Query = Buffer.Read8();
	if(!Connection->Authorized){
		if(Query == QUERY_LOGIN){
			ProcessLoginQuery(Connection, &Buffer);
		}else{
			LOG_ERR("Expected login query");
			CloseConnection(Connection);
		}
		return;
	}

	switch(Query){
		case QUERY_CHECK_ACCOUNT_PASSWORD:		ProcessCheckAccountPasswordQuery(Connection, &Buffer); break;
		case QUERY_LOGIN_ADMIN:					ProcessLoginAdminQuery(Connection, &Buffer); break;
		case QUERY_LOGIN_GAME:					ProcessLoginGameQuery(Connection, &Buffer); break;
		case QUERY_LOGOUT_GAME:					ProcessLogoutGameQuery(Connection, &Buffer); break;
		case QUERY_SET_NAMELOCK:				ProcessSetNamelockQuery(Connection, &Buffer); break;
		case QUERY_BANISH_ACCOUNT:				ProcessBanishAccountQuery(Connection, &Buffer); break;
		case QUERY_SET_NOTATION:				ProcessSetNotationQuery(Connection, &Buffer); break;
		case QUERY_REPORT_STATEMENT:			ProcessReportStatementQuery(Connection, &Buffer); break;
		case QUERY_BANISH_IP_ADDRESS:			ProcessBanishIpAddressQuery(Connection, &Buffer); break;
		case QUERY_LOG_CHARACTER_DEATH:			ProcessLogCharacterDeathQuery(Connection, &Buffer); break;
		case QUERY_ADD_BUDDY:					ProcessAddBuddyQuery(Connection, &Buffer); break;
		case QUERY_REMOVE_BUDDY:				ProcessRemoveBuddyQuery(Connection, &Buffer); break;
		case QUERY_DECREMENT_IS_ONLINE:			ProcessDecrementIsOnlineQuery(Connection, &Buffer); break;
		case QUERY_FINISH_AUCTIONS:				ProcessFinishAuctionsQuery(Connection, &Buffer); break;
		case QUERY_TRANSFER_HOUSES:				ProcessTransferHousesQuery(Connection, &Buffer); break;
		case QUERY_EVICT_FREE_ACCOUNTS:			ProcessEvictFreeAccountsQuery(Connection, &Buffer); break;
		case QUERY_EVICT_DELETED_CHARACTERS:	ProcessEvictDeletedCharactersQuery(Connection, &Buffer); break;
		case QUERY_EVICT_EX_GUILDLEADERS:		ProcessEvictExGuildleadersQuery(Connection, &Buffer); break;
		case QUERY_INSERT_HOUSE_OWNER:			ProcessInsertHouseOwnerQuery(Connection, &Buffer); break;
		case QUERY_UPDATE_HOUSE_OWNER:			ProcessUpdateHouseOwnerQuery(Connection, &Buffer); break;
		case QUERY_DELETE_HOUSE_OWNER:			ProcessDeleteHouseOwnerQuery(Connection, &Buffer); break;
		case QUERY_GET_HOUSE_OWNERS:			ProcessGetHouseOwnersQuery(Connection, &Buffer); break;
		case QUERY_GET_AUCTIONS:				ProcessGetAuctionsQuery(Connection, &Buffer); break;
		case QUERY_START_AUCTION:				ProcessStartAuctionQuery(Connection, &Buffer); break;
		case QUERY_INSERT_HOUSES:				ProcessInsertHousesQuery(Connection, &Buffer); break;
		case QUERY_CLEAR_IS_ONLINE:				ProcessClearIsOnlineQuery(Connection, &Buffer); break;
		case QUERY_CREATE_PLAYERLIST:			ProcessCreatePlayerlistQuery(Connection, &Buffer); break;
		case QUERY_LOG_KILLED_CREATURES:		ProcessLogKilledCreaturesQuery(Connection, &Buffer); break;
		case QUERY_LOAD_PLAYERS:				ProcessLoadPlayersQuery(Connection, &Buffer); break;
		case QUERY_EXCLUDE_FROM_AUCTIONS:		ProcessExcludeFromAuctionsQuery(Connection, &Buffer); break;
		case QUERY_CANCEL_HOUSE_TRANSFER:		ProcessCancelHouseTransferQuery(Connection, &Buffer); break;
		case QUERY_LOAD_WORLD_CONFIG:			ProcessLoadWorldConfigQuery(Connection, &Buffer); break;
		case QUERY_GET_KEPT_CHARACTERS:			ProcessGetKeptCharactersQuery(Connection, &Buffer); break;
		case QUERY_GET_DELETED_CHARACTERS:		ProcessGetDeletedCharactersQuery(Connection, &Buffer); break;
		case QUERY_DELETE_OLD_CHARACTER:		ProcessDeleteOldCharacterQuery(Connection, &Buffer); break;
		case QUERY_GET_HIDDEN_CHARACTERS:		ProcessGetHiddenCharactersQuery(Connection, &Buffer); break;
		case QUERY_CREATE_HIGHSCORES:			ProcessCreateHighscoresQuery(Connection, &Buffer); break;
		case QUERY_CREATE_CENSUS:				ProcessCreateCensusQuery(Connection, &Buffer); break;
		case QUERY_CREATE_KILL_STATISTICS:		ProcessCreateKillStatisticsQuery(Connection, &Buffer); break;
		case QUERY_GET_PLAYERS_ONLINE:			ProcessGetPlayersOnlineQuery(Connection, &Buffer); break;
		case QUERY_GET_WORLDS:					ProcessGetWorldsQuery(Connection, &Buffer); break;
		case QUERY_GET_SERVER_LOAD:				ProcessGetServerLoadQuery(Connection, &Buffer); break;
		case QUERY_INSERT_PAYMENT_DATA_OLD:		ProcessInsertPaymentDataOldQuery(Connection, &Buffer); break;
		case QUERY_ADD_PAYMENT_OLD:				ProcessAddPaymentOldQuery(Connection, &Buffer); break;
		case QUERY_CANCEL_PAYMENT_OLD:			ProcessCancelPaymentOldQuery(Connection, &Buffer); break;
		case QUERY_INSERT_PAYMENT_DATA_NEW:		ProcessInsertPaymentDataNewQuery(Connection, &Buffer); break;
		case QUERY_ADD_PAYMENT_NEW:				ProcessAddPaymentNewQuery(Connection, &Buffer); break;
		case QUERY_CANCEL_PAYMENT_NEW:			ProcessCancelPaymentNewQuery(Connection, &Buffer); break;
		default:{
			LOG_ERR("Unknown query %d from %s", Query, Connection->RemoteAddress);
			SendQueryStatusFailed(Connection);
			break;
		}
	}
}
