#include "querymanager.hh"

// Time
int  g_MonotonicTimeMS			= 0;

// Database CONFIG
char g_DatabaseFile[1024]		= "tibia.db";
int  g_MaxCachedStatements		= 100;

// Connection CONFIG
char g_Password[30]				= "";
int  g_Port						= 7174;
int  g_MaxConnections			= 50;
int  g_MaxConnectionIdleTime	= 60000;
int  g_MaxConnectionPacketSize	= (int)MB(1);
int  g_UpdateRate				= 20;

void LogAdd(const char *Prefix, const char *Format, ...){
	char Entry[4096];
	va_list ap;
	va_start(ap, Format);
	vsnprintf(Entry, sizeof(Entry), Format, ap);
	va_end(ap);

	if(Entry[0] != 0){
		struct tm LocalTime = GetLocalTime(time(NULL));
		fprintf(stdout, "%04d/%02d/%02d %02d:%02d:%02d [%s] %s\n",
				LocalTime.tm_year + 1900, LocalTime.tm_mon + 1, LocalTime.tm_mday,
				LocalTime.tm_hour, LocalTime.tm_min, LocalTime.tm_sec,
				Prefix, Entry);
	}
}

void LogAddVerbose(const char *Prefix, const char *Function,
		const char *File, int Line, const char *Format, ...){
	char Entry[4096];
	va_list ap;
	va_start(ap, Format);
	vsnprintf(Entry, sizeof(Entry), Format, ap);
	va_end(ap);

	if(Entry[0] != 0){
		(void)File;
		(void)Line;
		struct tm LocalTime = GetLocalTime(time(NULL));
		fprintf(stdout, "%04d/%02d/%02d %02d:%02d:%02d [%s] %s: %s\n",
				LocalTime.tm_year + 1900, LocalTime.tm_mon + 1, LocalTime.tm_mday,
				LocalTime.tm_hour, LocalTime.tm_min, LocalTime.tm_sec,
				Prefix, Function, Entry);
	}
}

struct tm GetLocalTime(time_t t){
	struct tm result;
#if COMPILER_MSVC
	localtime_s(&result, &t);
#else
	localtime_r(&t, &result);
#endif
	return result;
}

int64 GetClockMonotonicMS(void){
#if OS_WINDOWS
	LARGE_INTEGER Counter, Frequency;
	QueryPerformanceCounter(&Counter);
	QueryPerformanceFrequency(&Frequency);
	return (int64)((Counter.QuadPart * 1000) / Frequency.QuadPart);
#else
	struct timespec Time;
	clock_gettime(CLOCK_MONOTONIC, &Time);
	return ((int64)Time.tv_sec * 1000)
		+ ((int64)Time.tv_nsec / 1000000);
#endif
}

void SleepMS(int64 DurationMS){
#if OS_WINDOWS
	Sleep((DWORD)DurationMS);
#else
	struct timespec Duration;
	Duration.tv_sec = (time_t)(DurationMS / 1000);
	Duration.tv_nsec = (long)((DurationMS % 1000) * 1000000);
	nanosleep(&Duration, NULL);
#endif
}

bool StringEq(const char *A, const char *B){
	int Index = 0;
	while(true){
		if(A[Index] != B[Index]){
			return false;
		}else if(A[Index] == 0){
			return true;
		}
		Index += 1;
	}
}

bool StringEqCI(const char *A, const char *B){
	int Index = 0;
	while(true){
		if(tolower(A[Index]) != tolower(B[Index])){
			return false;
		}else if(A[Index] == 0){
			return true;
		}
		Index += 1;
	}
}

bool StringCopyN(char *Dest, int DestCapacity, const char *Src, int SrcLength){
	ASSERT(DestCapacity > 0);
	bool Result = (SrcLength < DestCapacity);
	if(Result && SrcLength > 0){
		memcpy(Dest, Src, SrcLength);
		Dest[SrcLength] = 0;
	}else{
		Dest[0] = 0;
	}
	return Result;
}

bool StringCopy(char *Dest, int DestCapacity, const char *Src){
	// IMPORTANT(fusion): `sqlite3_column_text` may return NULL if the column is
	// also NULL so we have an incentive to properly handle the case where `Src`
	// is NULL.
	int SrcLength = (Src != NULL ? (int)strlen(Src) : 0);
	return StringCopyN(Dest, DestCapacity, Src, SrcLength);
}

bool ReadBooleanConfig(bool *Dest, const char *Val){
	ASSERT(Dest && Val);
	*Dest = StringEqCI(Val, "true");
	return *Dest || StringEqCI(Val, "false");
}

bool ReadIntegerConfig(int *Dest, const char *Val){
	ASSERT(Dest && Val);
	const char *ValEnd;
	*Dest = (int)strtol(Val, (char**)&ValEnd, 0);
	return ValEnd > Val;
}

bool ReadDurationConfig(int *Dest, const char *Val){
	ASSERT(Dest && Val);
	const char *Suffix;
	*Dest = (int)strtol(Val, (char**)&Suffix, 0);
	if(Suffix == Val){
		return false;
	}

	while(Suffix[0] != 0 && isspace(Suffix[0])){
		Suffix += 1;
	}

	if(Suffix[0] == 'S' || Suffix[0] == 's'){
		*Dest *= (1000);
	}else if(Suffix[0] == 'M' || Suffix[0] == 'm'){
		*Dest *= (60 * 1000);
	}else if(Suffix[0] == 'H' || Suffix[0] == 'h'){
		*Dest *= (60 * 60 * 1000);
	}

	return true;
}

bool ReadSizeConfig(int *Dest, const char *Val){
	ASSERT(Dest && Val);
	const char *Suffix;
	*Dest = (int)strtol(Val, (char**)&Suffix, 0);
	if(Suffix == Val){
		return false;
	}

	while(Suffix[0] != 0 && isspace(Suffix[0])){
		Suffix += 1;
	}

	if(Suffix[0] == 'K' || Suffix[0] == 'k'){
		*Dest *= (1024);
	}else if(Suffix[0] == 'M' || Suffix[0] == 'm'){
		*Dest *= (1024 * 1024);
	}

	return true;
}

bool ReadStringConfig(char *Dest, int DestCapacity, const char *Val){
	ASSERT(Dest && DestCapacity > 0 && Val);
	int ValStart = 0;
	int ValEnd = (int)strlen(Val);
	if(ValEnd >= 2){
		if((Val[0] == '"' && Val[ValEnd - 1] == '"')
		|| (Val[0] == '\'' && Val[ValEnd - 1] == '\'')
		|| (Val[0] == '`' && Val[ValEnd - 1] == '`')){
			ValStart += 1;
			ValEnd -= 1;
		}
	}

	return StringCopyN(Dest, DestCapacity,
			&Val[ValStart], (ValEnd - ValStart));
}

bool ReadConfig(const char *FileName){
	FILE *File = fopen(FileName, "rb");
	if(File == NULL){
		LOG_ERR("Failed to open config file \"%s\"", FileName);
		return false;
	}

	bool EndOfFile = false;
	for(int LineNumber = 1; !EndOfFile; LineNumber += 1){
		char Line[1024];
		int MaxLineSize = (int)sizeof(Line);
		int LineSize = 0;
		int KeyStart = -1;
		int EqualPos = -1;
		while(true){
			int ch = fgetc(File);
			if(ch == EOF || ch == '\n'){
				if(ch == EOF){
					EndOfFile = true;
				}
				break;
			}

			if(LineSize < MaxLineSize){
				Line[LineSize] = (char)ch;
			}

			if(KeyStart == -1 && !isspace(ch)){
				KeyStart = LineSize;
			}

			if(EqualPos == -1 && ch == '='){
				EqualPos = LineSize;
			}

			LineSize += 1;
		}

		// NOTE(fusion): Check line size limit.
		if(LineSize > MaxLineSize){
			LOG_WARN("%s:%d: Exceeded line size limit of %d characters",
					FileName, LineNumber, MaxLineSize);
			continue;
		}

		// NOTE(fusion): Check empty line or comment.
		if(KeyStart == -1 || Line[KeyStart] == '#'){
			continue;
		}

		// NOTE(fusion): Check assignment.
		if(EqualPos == -1){
			LOG_WARN("%s:%d: No assignment found on non empty line",
					FileName, LineNumber);
			continue;
		}

		// NOTE(fusion): Check empty key.
		int KeyEnd = EqualPos;
		while(KeyEnd > KeyStart && isspace(Line[KeyEnd - 1])){
			KeyEnd -= 1;
		}

		if(KeyStart == KeyEnd){
			LOG_WARN("%s:%d: Empty key", FileName, LineNumber);
			continue;
		}

		// NOTE(fusion): Check empty value.
		int ValStart = EqualPos + 1;
		int ValEnd = LineSize;
		while(ValStart < ValEnd && isspace(Line[ValStart])){
			ValStart += 1;
		}

		while(ValEnd > ValStart && isspace(Line[ValEnd - 1])){
			ValEnd -= 1;
		}

		if(ValStart == ValEnd){
			LOG_WARN("%s:%d: Empty value", FileName, LineNumber);
			continue;
		}

		// NOTE(fusion): Parse KV pair.
		char Key[256];
		if(!StringCopyN(Key, (int)sizeof(Key), &Line[KeyStart], (KeyEnd - KeyStart))){
			LOG_WARN("%s:%d: Exceeded key size limit of %d characters",
					FileName, LineNumber, (int)(sizeof(Key) - 1));
			continue;
		}

		char Val[256];
		if(!StringCopyN(Val, (int)sizeof(Val), &Line[ValStart], (ValEnd - ValStart))){
			LOG_WARN("%s:%d: Exceeded value size limit of %d characters",
					FileName, LineNumber, (int)(sizeof(Val) - 1));
			continue;
		}

		if(StringEqCI(Key, "DatabaseFile")){
			ReadStringConfig(g_DatabaseFile, (int)sizeof(g_DatabaseFile), Val);
		}else if(StringEqCI(Key, "MaxCachedStatements")){
			ReadIntegerConfig(&g_MaxCachedStatements, Val);
		}else if(StringEqCI(Key, "Password")){
			ReadStringConfig(g_Password, (int)sizeof(g_Password), Val);
		}else if(StringEqCI(Key, "Port")){
			ReadIntegerConfig(&g_Port, Val);
		}else if(StringEqCI(Key, "MaxConnections")){
			ReadIntegerConfig(&g_MaxConnections, Val);
		}else if(StringEqCI(Key, "MaxConnectionIdleTime")){
			ReadDurationConfig(&g_MaxConnectionIdleTime, Val);
		}else if(StringEqCI(Key, "MaxConnectionPacketSize")){
			ReadSizeConfig(&g_MaxConnectionPacketSize, Val);
		}else if(StringEqCI(Key, "UpdateRate")){
			ReadIntegerConfig(&g_UpdateRate, Val);
		}else{
			LOG_WARN("Unknown config \"%s\"", Key);
		}
	}

	fclose(File);
	return true;
}

int main(int argc, const char **argv){
	(void)argc;
	(void)argv;

	int64 StartTime = GetClockMonotonicMS();
	g_MonotonicTimeMS = 0;

	LOG("Tibia Query Manager v0.1");
	if(!ReadConfig("config.cfg")){
		return EXIT_FAILURE;
	}

	atexit(ExitDatabase);
	atexit(ExitConnections);
	if(!InitDatabase() || !InitConnections()){
		return EXIT_FAILURE;
	}

	LOG("Running at %d updates per second...", g_UpdateRate);
	int64 UpdateInterval = 1000 / (int64)g_UpdateRate;
	while(true){
		int64 UpdateStart = GetClockMonotonicMS();
		g_MonotonicTimeMS = (int)(UpdateStart - StartTime);
		ProcessConnections();
		int64 UpdateEnd = GetClockMonotonicMS();
		int64 NextUpdate = UpdateStart + UpdateInterval;
		if(NextUpdate > UpdateEnd){
			SleepMS(NextUpdate - UpdateEnd);
		}
	}

	return EXIT_SUCCESS;
}
