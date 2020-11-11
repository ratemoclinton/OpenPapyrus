// SLSESS.CPP
// Copyright (c) A.Sobolev 2003, 2005, 2006, 2007, 2008, 2009, 2010, 2013, 2014, 2015, 2016, 2017, 2018, 2019, 2020
// @codepage UTF-8
//
#include <slib-internal.h>
#pragma hdrstop
#include <locale.h> // setlocale()
// htmlhelp.h {
#define HH_INITIALIZE            0x001C  // Initializes the help system.
#define HH_UNINITIALIZE          0x001D  // Uninitializes the help system.
// } htmlhelp.h
#define USE_OPENSSL_STATIC
//
const SlConstParam _SlConst;
//
//
//
SlExtraProcBlock::SlExtraProcBlock()
{
	Reset();
}

void SlExtraProcBlock::Reset()
{
	F_LoadString = 0;
	F_ExpandString = 0;
	F_CallHelp = 0;
	F_CallCalc = 0;
	F_CallCalendar = 0;
	F_GetGlobalSecureConfig = 0;
	F_GetDefaultEncrKey = 0;
	F_QueryPath = 0; // @v9.8.7
}

void SlExtraProcBlock::Set(const SlExtraProcBlock * pS)
{
	if(pS) {
		F_LoadString = pS->F_LoadString;
		F_ExpandString = pS->F_ExpandString;
		F_CallHelp = pS->F_CallHelp;
		F_CallCalc = pS->F_CallCalc;
		F_CallCalendar = pS->F_CallCalendar;
		F_GetGlobalSecureConfig = pS->F_GetGlobalSecureConfig;
		F_GetDefaultEncrKey = pS->F_GetDefaultEncrKey;
		F_QueryPath = pS->F_QueryPath; // @v9.8.7
	}
	else
		Reset();
}
//
//
//
SlThreadLocalArea::SlThreadLocalArea() : Prf(1), Id(0), LastErr(0), LastOsErr(0), LastSockErr(0), LastCurlErr(0),
	BinDateFmt_(DEFAULT_BIN_DATE_FORMAT), TxtDateFmt_(DEFAULT_TXT_DATE_FORMAT), CurrentCp(cpUndef), UiFlags(0), UiLanguageId(-1),
	SAry_OrgFCMP(0), SAry_PtrContainer(0), SAry_SortExtraData(0), FontDc(0), P_Rez(0), RvlSStA(1024), RvlSStW(1024)
{
	const LDATETIME now_time = getcurdatetime_();
	{
		DefaultYear_  = now_time.d.year();
		DefaultMonth_ = now_time.d.month();
	}
	// @v9.6.5 memzero(OneCStrBuf, sizeof(OneCStrBuf));
	Rg.Set(now_time.d.v ^ now_time.t.v);
	NextDialogLuPos.Set(-1, -1);
}

SlThreadLocalArea::~SlThreadLocalArea()
{
	Destroy();
}

void SlThreadLocalArea::Destroy()
{
	ZDELETE(P_Rez);
	if(FontDc)
		::DeleteDC(FontDc);
	RemoveTempFiles();
}

TVRez * SlThreadLocalArea::GetRez()
{
	if(!P_Rez) {
		SString name;
		makeExecPathFileName("pp", "res", name);
		if(fileExists(name))
			P_Rez = new TVRez(name, 1);
	}
	return P_Rez;
}

void SlThreadLocalArea::SetNextDialogLuPos(int left, int top)
{
	NextDialogLuPos.Set(left, top);
}

TPoint SlThreadLocalArea::GetNextDialogLuPos()
{
	TPoint result = NextDialogLuPos;
	NextDialogLuPos.Set(-1, -1);
	return result;
}

HDC SlThreadLocalArea::GetFontDC()
{
	if(!FontDc) {
		FontDc = ::CreateCompatibleDC(0);
		if(FontDc && !::SetGraphicsMode(FontDc, GM_ADVANCED)) {
			::DeleteDC(FontDc);
			FontDc = 0;
		}
	}
	return FontDc;
}

int SlThreadLocalArea::RegisterTempFileName(const char * pFileName)
{
	return isempty(pFileName) ? -1 : TempFileList.add(pFileName);
}

void SlThreadLocalArea::RemoveTempFiles()
{
	SString file_name;
	StringSet temp_list;
	for(uint i = 0; TempFileList.get(&i, file_name);)
		if(!SFile::Remove(file_name))
			temp_list.add(file_name);
	TempFileList = temp_list;
}

SlSession::SlSession() : SSys(1), Id(1), TlsIdx(-1), StopFlag(0), P_StopEvnt(0), DragndropObjIdx(0), GlobSymbList(512, 0), // @v9.8.1 256-->512
	WsaInitCounter(0), HelpCookie(0), UiLanguageId(0)
{
	assert((void *)&TlsIdx == (void *)this); // TlsIdx - @firstmember
#if (USE_ASMLIB > 0)
	{
		//
		// Перед началом исполнения программы сделаем вызовы функций из библиотеки ASMLIB для того,
		// чтобы они сразу инициализировали внутренние таблицы, зависящие от процессора.
		// Таким образом, мы избежим риска конфликтов при многопоточном исполнении.
		//
		const  size_t S = 128;
		char   temp_buf1[S], temp_buf2[S];
		A_memset(temp_buf1, 0, S);
		A_memset(temp_buf2, 0, S);
		A_memmove(temp_buf1, temp_buf2, S);
		A_memcpy(temp_buf2, temp_buf1, S);
		A_memset(temp_buf2, '1', S/4);
		temp_buf1[0] = 0;
		//strcat(temp_buf1, "0");
		//xeos_memchr(temp_buf1, '0', xeos_strlen(temp_buf1));
		/* @v9.0.6
		A_strlen(temp_buf2);
		A_strcpy(temp_buf1, temp_buf2);
		A_strcmp(temp_buf1, temp_buf2);
		A_stricmp(temp_buf1, temp_buf2);
		A_strstr(temp_buf1, "11");
		*/
	}
#endif
	// @v9.8.1 LastThread.Assign(0);
	ExtraProcBlk.Reset();
	SessUuid.Generate(); // Генерируем абсолютно уникальный id сессии.
	TlsIdx = TlsAlloc();
	InitThread();
}

SlSession::~SlSession()
{
	if(ExtraProcBlk.F_CallHelp && HelpCookie) {
		ExtraProcBlk.F_CallHelp(0, HH_UNINITIALIZE, HelpCookie);
	}
	ReleaseThread();
	TlsFree(TlsIdx);
	delete P_StopEvnt;
	for(int i = 0; i < WsaInitCounter; i++)
		WSACleanup();
#ifndef __GENERIC_MAIN_CONDUIT__
	ShutdownGdiplus();
#endif
}

int SlSession::CheckStopFlag() const
{
	return BIN(StopFlag);
}

int SlSession::Stop()
{
	int    ok = -1;
	if(!StopFlag) {
		ENTER_CRITICAL_SECTION
		if(!StopFlag) {
			StopFlag = 1;
			CALLPTRMEMB(P_StopEvnt, Signal());
			ok = 1;
		}
		LEAVE_CRITICAL_SECTION
	}
	return ok;
}

int SlSession::ResetStopState()
{
	int    ok = -1;
	if(StopFlag) {
		ENTER_CRITICAL_SECTION
		if(StopFlag) {
			StopFlag = 0;
			if(P_StopEvnt) {
				P_StopEvnt->Reset();
				ZDELETE(P_StopEvnt);
			}
			ok = 1;
		}
		LEAVE_CRITICAL_SECTION
	}
	return ok;
}

static void InitTest()
{
#ifndef NDEBUG // {
	{
		//
		// @paranoic (Эта проверка нужна мне для успокоения, ибо меня преследует фобия, что такое равенство не выполняется)
		//
		char   temp_buf[32];
		assert((void *)temp_buf == (void *)&temp_buf);
	}
	{
		//
		// Проверка компилятора не предмет однозначного равенства результатов сравнения 0 или 1.
		//
		int    ix;
		double rx;
		void * p_x = 0;
		ix = 0;
		assert((ix == 0) == 1);
		assert((ix != 0) == 0);
		assert((ix > 0) == 0);
		assert((ix <= 0) == 1);
		ix = 93281;
		assert((ix == 93281) == 1);
		assert((ix != 93281) == 0);
		rx = 0.0;
		assert((rx == 0) == 1);
		assert((rx != 0) == 0);
		rx = 17.5;
		assert((rx == 17.5) == 1);
		assert((rx != 17.5) == 0);
		p_x = 0;
		assert((p_x == 0) == 1);
		assert((p_x != 0) == 0);
		p_x = &rx;
		assert((p_x == &rx) == 1);
		assert((p_x != &rx) == 0);
	}
	{
		// @paranoic (Защита от классической шутки)
		assert(TRUE == 1);
		assert(FALSE == 0);
		assert(true == 1);
		assert(false == 0);
	}
	{
		//
		// Тестирование макроса SETIFZ
		//
		int    a = 1;
		SETIFZ(a, 2);
		assert(a == 1);
		a = 0;
		SETIFZ(a, 2);
		assert(a == 2);
		{
			void * ptr = 0;
			if(SETIFZ(ptr, SAlloc::M(128))) {
				assert(ptr != 0);
			}
			else {
				assert(ptr == 0);
			}
			ZFREE(ptr);
			//
			const char * p_abc = "abc";
			ptr = (void *)p_abc;
			if(SETIFZ(ptr, SAlloc::M(128))) { // Memory hasn't been allocated (ptr != 0)
				assert(ptr == p_abc);
			}
			else {
				assert(ptr == 0);
			}
			ptr = 0;
			p_abc = 0;
			if(SETIFZ(ptr, (void *)p_abc)) {
				assert(0);
			}
			else {
				assert(ptr == p_abc);
			}
		}
	}
	{
		//
		// Удостоверяемся в том, что SIZEOFARRAY работает правильно (тоже фобия)
		//
		struct TestStruc {
			const char * P_S;
			int16  I16;
		};
		TestStruc test_array[] = {
			{ "Abc", 1 },
			{ "Ab2", 2 },
			{ "Ab3", 3 },
			{ "Ab4", 4 },
			{ "Ab5", 5 }
		};
		assert(SIZEOFARRAY(test_array) == 5);
	}
	assert(sizeof(bool) == 1);
	assert(sizeof(char) == 1);
	assert(sizeof(int) == 4);
	assert(sizeof(uint) == 4);
	assert(sizeof(short) >= 2); // @v10.2.3
	assert(sizeof(long)  >= 4); // @v10.2.3
	assert(sizeof(int) >= sizeof(short)); // @v10.2.3
	assert(sizeof(long) >= sizeof(int)); // @v10.2.3
	assert(sizeof(int8) == 1);
	assert(sizeof(uint8) == 1);
	assert(sizeof(int16) == 2);
	assert(sizeof(uint16) == 2);
	assert(sizeof(int32) == 4);
	assert(sizeof(uint32) == 4);
	assert(sizeof(int64) == 8);
	assert(sizeof(uint64) == 8);
	assert(sizeof(float) == 4); // @v10.7.11
	assert(sizeof(double) == 8); // @v10.7.11
	assert(sizeof(S_GUID) == 16);
	assert(sizeof(SColorBase) == 4); // @v10.2.4
	assert(sizeof(SColor) == sizeof(SColorBase)); // @v10.2.4
	assert(sizeof(IntRange) == 8);
	assert(sizeof(RealRange) == 16);
	assert(sizeof(DateRange) == 8); // @v10.2.4
	assert(sizeof(TimeRange) == 8); // @v10.2.4
#ifdef _M_X64
	assert(sizeof(SBaseBuffer) == 16);
#else
	assert(sizeof(SBaseBuffer) == 8);
#endif
	assert(sizeof(DateRepeating) == 8);
	assert(sizeof(DateTimeRepeating) == 12);
	//
	assert(sizeof(TYPEID) == 4);
	assert(sizeof(STypEx) == 16);
	assert(sizeof(CommPortParams) == 6);
	//
	assert(MAX(3.1, 8.5) == 8.5);
	assert(smax(3.1, 8.5) == 8.5);
	assert(smax(3.1f, 8.5f) == 8.5f);
	assert(MIN(1.5, -7.3) == -7.3);
	assert(smin(1.5, -7.3) == -7.3);
	assert(smin(1.5f, -7.3f) == -7.3f);
	assert(smin(1, 2) == 1);
	assert(smax(-5, 5) == 5);
	{
		const long test_dword = 0x1234befa;
		assert(MakeLong(LoWord(test_dword), HiWord(test_dword)) == test_dword);
	}
	{
		//
		// Проверка макроса SETIFZ для даты
		//
		const LDATE cdt = getcurdate_();
		LDATE dt = ZERODATE;
		SETIFZ(dt, cdt);
		assert(dt == cdt);
		dt = encodedate(7, 11, 2017);
		SETIFZ(dt, cdt);
		assert(dt != cdt);
	}
	{
		//
		// Проверка макроса SETIFZ для LDATETIME
		//
		const LDATETIME cdtm = getcurdatetime_();
		LDATETIME dtm = ZERODATETIME;
		SETIFZ(dtm, cdtm);
		assert(dtm == cdtm);
		dtm.d = encodedate(7, 11, 2017);
		dtm.t = encodetime(12, 25, 58, 9);
		SETIFZ(dtm, cdtm);
		assert(dtm != cdtm);
	}
	// @v10.7.9 {
	{
		{
			SString temp_buf;
			StringSet ss(';', "1;2");
			ss.get(static_cast<size_t>(0), temp_buf);
			assert(temp_buf == "1");
		}
		{
			SString temp_buf;
			StringSet(';', "1;2").get(static_cast<size_t>(0), temp_buf);
			assert(temp_buf == "1");
		}
	}
	// } @v10.7.9 
#endif // } NDEBUG
}

void SlSession::Init(const char * pAppName, HINSTANCE hInst)
{
	/*
	{
		//
		// Перед началом исполнения программы сделаем вызовы функций из библиотеки ASMLIB для того,
		// чтобы они сразу инициализировали внутренние таблицы, зависящие от процессора.
		// Таким образом, мы избежим риска конфликтов при многопоточном исполнении.
		//
		const  size_t S = 128;
		char   temp_buf1[S], temp_buf2[S];
		A_memset(temp_buf1, 0, S);
		A_memset(temp_buf2, 0, S);
		A_memmove(temp_buf1, temp_buf2, S);
		A_memcpy(temp_buf2, temp_buf1, S);
		A_memset(temp_buf2, '1', S/4);
		A_strlen(temp_buf2);
		A_strcpy(temp_buf1, temp_buf2);
		A_strcmp(temp_buf1, temp_buf2);
		A_stricmp(temp_buf1, temp_buf2);
		A_strstr(temp_buf1, "11");
	}
	*/
	H_Inst = NZOR(hInst, GetModuleHandle(0));
	// @v10.3.9 char   exe_path[MAXPATH];
	// @v10.3.9 GetModuleFileName(H_Inst, exe_path, sizeof(exe_path)); // @unicodeproblem
	// @v10.3.9 ExePath = exe_path;
	SSystem::SGetModuleFileName(H_Inst, ExePath); // @v10.3.9
	AppName = pAppName;
	if(AppName.NotEmpty()) {
		SString n;
		P_StopEvnt = new Evnt(GetStopEventName(n), Evnt::modeCreate);
	}
	RegisterBIST();
	SFileFormat::Register();
	InitTest();
}

void SlSession::SetAppName(const char * pAppName)
{
	AppName = pAppName;
	if(!P_StopEvnt && AppName.NotEmpty()) {
		SString n;
		P_StopEvnt = new Evnt(GetStopEventName(n), Evnt::modeCreate);
	}
}

int SlSession::InitWSA()
{
	int    ok = 1;
	ENTER_CRITICAL_SECTION
	WSADATA wsa_data;
	if(WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
		ok = SetError(SLERR_SOCK_WINSOCK);
	else
		WsaInitCounter++;
	LEAVE_CRITICAL_SECTION
	return ok;
}

const void * SlSession::InitThread()
{
	SlThreadLocalArea * p_tla = new SlThreadLocalArea;
	TlsSetValue(TlsIdx, p_tla);
	p_tla->Id = GetCurrentThreadId();
	return static_cast<const void *>(p_tla);
}

void SlSession::ReleaseThread()
{
	SlThreadLocalArea * p_tla = static_cast<SlThreadLocalArea *>(TlsGetValue(TlsIdx));
	if(p_tla) {
		TlsSetValue(TlsIdx, 0);
		delete p_tla;
	}
}

/* (inlined) void * FASTCALL SGetTls(const long idx)
{
#ifdef NDEBUG
	return TlsGetValue(idx);
#else
	void * p = TlsGetValue(idx);
	if(p)
		return p;
	else {
		assert(0);
		return 0;
	}
#endif
}*/

// (inlined) SlThreadLocalArea & SlSession::GetTLA() { return *(SlThreadLocalArea *)SGetTls(TlsIdx); }
// (inlined) const SlThreadLocalArea & SlSession::GetConstTLA() const { return *(SlThreadLocalArea *)SGetTls(TlsIdx); }

int FASTCALL SlSession::SetError(int errCode, const char * pAddedMsg)
{
	const int sock_err = (errCode == SLERR_SOCK_WINSOCK) ? WSAGetLastError() : 0;
	SlThreadLocalArea & r_tla = GetTLA();
	if(&r_tla) {
		//
		// @1 Если глобальный объект SLS разрушается раньше иных глобальных объектов,
		// которые могут вызвать SlSession::SetError, то при завершении процесса может возникнуть исключение
		// обращения к нулевому адресу. Во избежании этого проверяем &r_tla на 0.
		//
		r_tla.LastErr = errCode;
		r_tla.AddedMsgString = pAddedMsg;
		r_tla.LastSockErr = sock_err;
	}
	return 0;
}

int FASTCALL SlSession::SetError(int errCode)
{
	const int sock_err = (errCode == SLERR_SOCK_WINSOCK) ? WSAGetLastError() : 0;
	SlThreadLocalArea & r_tla = GetTLA();
	if(&r_tla) {
		//
		// see @1 above
		//
		r_tla.LastErr = errCode;
		r_tla.AddedMsgString = 0;
		r_tla.LastSockErr = sock_err;
	}
	return 0;
}

int FASTCALL SlSession::SetOsError(const char * pAddedMsg)
{
	const int last_err = ::GetLastError();
	SlThreadLocalArea & r_tla = GetTLA();
	if(&r_tla) {
		//
		// see @1 above
		//
		r_tla.LastErr = SLERR_WINDOWS;
		r_tla.LastOsErr = last_err;
		r_tla.AddedMsgString = pAddedMsg;
	}
	return 0;
}

int    SlSession::GetOsError() const { return GetConstTLA().LastOsErr; }
const  SString & SlSession::GetAddedMsgString() const { return GetConstTLA().AddedMsgString; }
const  SString & SlSession::GetExePath() const { return ExePath; }
const  SString & SlSession::GetAppName() const { return AppName; }
void   FASTCALL SlSession::SetAddedMsgString(const char * pStr) { GetTLA().AddedMsgString = pStr; }

void SlSession::SetUiLanguageId(int languageId, int currentThreadOnly)
{
	if(currentThreadOnly) {
		GetTLA().UiLanguageId = languageId;
	}
	else {
		ENTER_CRITICAL_SECTION
		UiLanguageId = languageId;
		LEAVE_CRITICAL_SECTION
	}
}

int  SlSession::GetUiLanguageId() const
{
	int    lid = GetConstTLA().UiLanguageId;
	return (lid < 0) ? UiLanguageId : lid;
}

SString & SlSession::GetStopEventName(SString & rBuf) const
{
	SString temp_buf;
	SessUuid.ToStr(S_GUID::fmtIDL, temp_buf);
	return rBuf.Z().Cat(AppName).CatChar('_').Cat("Stop").CatChar('_').Cat(temp_buf);
}

int    SlSession::RegisterTempFileName(const char * pFileName) { return GetTLA().RegisterTempFileName(pFileName); }
void   SlSession::RemoveTempFiles() { GetTLA().RemoveTempFiles(); }
void   SlSession::SetLogPath(const char * pPath) { GetTLA().LogPath = pPath; }
SString & SlSession::GetLogPath(SString & rPath) const { return (rPath = GetConstTLA().LogPath); }
//
//
//
struct GlobalObjectEntry {
	void   FASTCALL operator = (SClassWrapper & rCls)
	{
		VT = *reinterpret_cast<void **>(&rCls);
	}
	int    Create()
	{
		if(VT) {
			uint8  stub[32];
			SClassWrapper * p_cls = reinterpret_cast<SClassWrapper *>(stub);
			*reinterpret_cast<void **>(p_cls) = VT;
			Ptr = p_cls->Create();
		}
		return (Ptr != 0);
	}
	void   Destroy()
	{
		if(VT && Ptr) {
			uint8  stub[32];
			SClassWrapper * p_cls = reinterpret_cast<SClassWrapper *>(stub);
			*reinterpret_cast<void **>(p_cls) = VT;
			p_cls->Destroy(Ptr);
		}
		Ptr = 0;
	}
	void * VT;
	void * Ptr;
};

SlSession::GlobalObjectArray::GlobalObjectArray() : SVector(sizeof(GlobalObjectEntry)) // @v9.8.5 SArray-->SVector
{
	//
	// Дабы не использовать нулевой индекс вставляем фиктивный первый элемент.
	//
	TSClassWrapper <int> zero_cls;
	GlobalObjectEntry zero_entry;
	zero_entry.VT = *reinterpret_cast<void **>(&zero_cls);
	zero_entry.Ptr = 0;
	insert(&zero_entry);
}

SlSession::GlobalObjectArray::~GlobalObjectArray()
{
	Cs.Enter();
	for(uint i = 1; i < count; i++) {
		GlobalObjectEntry * p_entry = static_cast<GlobalObjectEntry *>(at(i));
		CALLPTRMEMB(p_entry, Destroy());
	}
	Cs.Leave();
}

uint SlSession::GlobalObjectArray::CreateObject(SClassWrapper & rCls)
{
	uint   new_idx = 0;
	assert(count > 0);
	Cs.Enter();
	for(uint i = 1; !new_idx && i < count; i++) {
		GlobalObjectEntry * p_entry = static_cast<GlobalObjectEntry *>(at(i));
		if(p_entry->Ptr == 0) {
			*p_entry = rCls;
			THROW_S(p_entry->Create(), SLERR_NOMEM);
			new_idx = i;
		}
	}
	if(!new_idx) {
		GlobalObjectEntry new_entry;
		new_entry = rCls;
		THROW_S(new_entry.Create(), SLERR_NOMEM);
		THROW(insert(&new_entry));
		new_idx = getCount()-1;
	}
	CATCH
		new_idx = 0;
	ENDCATCH
	Cs.Leave();
	return new_idx;
}

int SlSession::GlobalObjectArray::DestroyObject(uint idx)
{
	int    ok = 1;
	Cs.Enter();
	if(idx && idx < count)
		static_cast<GlobalObjectEntry *>(at(idx))->Destroy();
	Cs.Leave();
	return ok;
}

void * FASTCALL SlSession::GlobalObjectArray::GetObject(uint idx)
{
	void * ptr = 0;
	Cs.Enter();
	if(idx && idx < getCount())
		ptr = static_cast<GlobalObjectEntry *>(at(idx))->Ptr;
	Cs.Leave();
	if(!ptr) {
		SString temp_buf;
		SLS.SetError(SLERR_GLOBOBJIDXNFOUNT, temp_buf.Cat(idx));
	}
	return ptr;
}

uint   SlSession::CreateGlobalObject(SClassWrapper & rCls) { return GlobObjList.CreateObject(rCls); }
int    SlSession::DestroyGlobalObject(uint idx) { return GlobObjList.DestroyObject(idx); }
void * FASTCALL SlSession::GetGlobalObject(uint idx) { return GlobObjList.GetObject(idx); }
int64  SlSession::GetSequenceValue() { return SeqValue.Incr(); }
uint64 SlSession::GetProfileTime() { return GetTLA().Prf.GetAbsTimeMicroseconds(); }

long SlSession::GetGlobalSymbol(const char * pSymb, long ident, SString * pRetSymb) // @cs
{
	long   _i = 0;
	// (здесь нельзя использовать макрос из-за зацикливания при трассировке блокировок) ENTER_CRITICAL_SECTION
	{
		static SCriticalSection::Data __csd(1);
		SCriticalSection __cs(__csd);
		uint   val = 0;
		if(pSymb) {
			if(GlobSymbList.Search(pSymb, &val, 0)) {
				_i = static_cast<long>(val);
				assert(ident <= 0 || _i == ident);
				if(ident > 0 && _i != ident) {
					_i = 0;
				}
			}
			else if(ident >= 0) {
				val = (uint)NZOR(ident, /*LastGlobSymbId*/SeqValue.Incr()); // @v9.8.1 LastGlobSymbId-->SeqValue
				if(GlobSymbList.Add(pSymb, val, 0)) {
					_i = static_cast<long>(val);
				}
			}
			else
				_i = -1;
		}
		else if(ident > 0) {
			SString temp_buf;
			SString * p_ret_symb = NZOR(pRetSymb, &temp_buf);
			if(GlobSymbList.GetByAssoc(ident, *p_ret_symb)) {
				_i = ident;
			}
		}
	}
	//LEAVE_CRITICAL_SECTION
	return _i;
}

long  SlSession::SetUiFlag(long f, int set)
{
    SlThreadLocalArea & r_tla = GetTLA();
    const long prev_ui_flags = r_tla.UiFlags;
    SETFLAG(r_tla.UiFlags, f, set);
    return prev_ui_flags;
}

int   FASTCALL SlSession::CheckUiFlag(long f) const { return BIN((GetConstTLA().UiFlags & f) == f); }

struct DdoEntry {
	DdoEntry() : Type(0), P_Obj(0)
	{
	}
	int    Type;
	void * P_Obj;
};

int SlSession::SetupDragndropObj(int ddoType, void * pObj)
{
	int    ok = 1;
	ENTER_CRITICAL_SECTION
	if(DragndropObjIdx) {
		DestroyGlobalObject(DragndropObjIdx);
		DragndropObjIdx = 0;
	}
	if(ddoType) {
		TSClassWrapper <DdoEntry> ptr_cls;
		DragndropObjIdx = CreateGlobalObject(ptr_cls);
        DdoEntry * p_item = DragndropObjIdx ? static_cast<DdoEntry *>(SLS.GetGlobalObject(DragndropObjIdx)) : 0;
		if(p_item) {
			 p_item->Type = ddoType;
			 p_item->P_Obj = pObj;
		}
		else
			ok = 0;
	}
	LEAVE_CRITICAL_SECTION
	return ok;
}

int SlSession::IsThereDragndropObj(void ** ppObj)
{
	int    type = 0;
	ENTER_CRITICAL_SECTION
	if(DragndropObjIdx) {
		DdoEntry * p_item = static_cast<DdoEntry *>(SLS.GetGlobalObject(DragndropObjIdx));
		if(p_item) {
			ASSIGN_PTR(ppObj, p_item->P_Obj);
			type = p_item->Type;
		}
	}
	LEAVE_CRITICAL_SECTION
	return type;
}

SGlobalSecureConfig::SGlobalSecureConfig() : Flags(0)
{
}

int SGlobalSecureConfig::IsEmpty() const { return BIN(Flags == 0 && CaFile.Empty() && CaPath.Empty()); }

const SGlobalSecureConfig & SlSession::GetGlobalSecureConfig()
{
	SlThreadLocalArea & r_tla = GetTLA();
	SGlobalSecureConfig & r_cfg = r_tla.Gsc;
	if(r_cfg.IsEmpty()) {
		if(ExtraProcBlk.F_GetGlobalSecureConfig) {
			ExtraProcBlk.F_GetGlobalSecureConfig(&r_cfg);
		}
		if(r_cfg.IsEmpty()) {
			SString temp_buf;
			getExecPath(temp_buf).SetLastSlash();
			r_cfg.CaPath = temp_buf;
			r_cfg.CaFile = temp_buf.Cat("cacerts.pem");
		}
	}
	return r_cfg;
}
//
//
//
int SlSession::LogMessage(const char * pFileName, const char * pStr, ulong maxFileSize)
{
	int    ok = 1;
	long   current_size = 0;
	FILE * f = 0;
	SString file_name(pFileName);
	{
		SPathStruc ps;
		if(file_name.NotEmptyS())
			ps.Split(file_name);
		ps.Nam.SetIfEmpty("slib");
		ps.Ext.SetIfEmpty("log");
		if(ps.Drv.Empty() && ps.Dir.Empty()) {
			GetLogPath(file_name);
			if(!file_name.NotEmptyS()) {
				getExecPath(file_name);
			}
			file_name.SetLastSlash().Cat(ps.Nam);
			if(ps.Ext.C(0) != '.')
				file_name.Dot();
			file_name.Cat(ps.Ext);
		}
		else
			ps.Merge(file_name);
	}
	f = fopen(file_name, "r");
	if(f) {
		fseek(f, 0, SEEK_END);
		current_size = ftell(f);
		fclose(f);
		f = 0;
	}
	else {
		f = fopen(file_name, "w");
		if(f)
			fclose(f);
		else
			ok = 0;
	}
	if(ok) {
		SString msg_buf;
		(msg_buf = pStr).CR();
		ENTER_CRITICAL_SECTION
			int    counter;
			if(maxFileSize && (current_size + msg_buf.Len()) > maxFileSize*1024) {
				counter = 0;
				SString ext, b = file_name;
				do {
					SPathStruc::ReplaceExt(b, ext.Z().CatLongZ(++counter, 3), 1);
				} while(fileExists(b));
				copyFileByName(file_name, b);
				SFile::Remove(file_name);
			}
			counter = 30;
			do {
				if(!(f = fopen(file_name, "a+"))) {
					if(CheckStopFlag()) {
						ok = 0;
						break;
					}
					Sleep(10);
				}
			} while(!f && --counter);
			if(f) {
				fputs(msg_buf, f);
				fclose(f);
			}
		LEAVE_CRITICAL_SECTION
	}
	return ok;
}

int SlSession::LoadString_(const char * pSignature, SString & rBuf) const
{
	LoadStringFunc f_ls = ExtraProcBlk.F_LoadString;
	return f_ls ? f_ls(pSignature, rBuf) : 0;
}

int SlSession::ExpandString(SString & rBuf, int ctransf) const
{
	ExpandStringFunc f_es = ExtraProcBlk.F_ExpandString;
	return f_es ? f_es(rBuf, ctransf) : 0;
}

int SlSession::QueryPath(const char * pSignature, SString & rBuf) const
{
    QueryPathFunc f_qp = ExtraProcBlk.F_QueryPath;
    return f_qp ? f_qp(pSignature, rBuf) : (rBuf.Z(), 0);
}

void SlSession::GetExtraProcBlock(SlExtraProcBlock * pBlk) const
{
	ASSIGN_PTR(pBlk, ExtraProcBlk);
}

void SlSession::SetExtraProcBlock(const SlExtraProcBlock * pBlk)
{
	ENTER_CRITICAL_SECTION
	ExtraProcBlk.Set(pBlk);
	LEAVE_CRITICAL_SECTION
}

void SlSession::LockPush(int lockType, const char * pSrcFileName, uint srcLineNo)
{
	SlThreadLocalArea & r_tla = GetTLA();
	if(&r_tla)
		r_tla.LckStk.Push(lockType, pSrcFileName, srcLineNo);
}

void SlSession::LockPop()
{
	SlThreadLocalArea & r_tla = GetTLA();
	if(&r_tla)
		r_tla.LckStk.Pop();
}

SString  & SlSession::AcquireRvlStr() { return GetTLA().RvlSStA.Get(); }
SStringU & SlSession::AcquireRvlStrU() { return GetTLA().RvlSStW.Get(); }

#if 0 // @v9.1.2 replaced by SetExtraProcBlock() {
int SlSession::SetGlobalSecureConfigFunc(GetGlobalSecureConfigFunc fProc)
{
	F_GetGlobalSecureConfig = fProc;
	return 1;
}

int SlSession::SetLoadStringFunc(LoadStringFunc fProc)
{
	ENTER_CRITICAL_SECTION
	F_LoadString = fProc;
	LEAVE_CRITICAL_SECTION
	return 1;
}

int SlSession::SetExpandStringFunc(ExpandStringFunc fProc)
{
	ENTER_CRITICAL_SECTION
	F_ExpandString = fProc;
	LEAVE_CRITICAL_SECTION
	return 1;
}

int SlSession::SetCallHelpFunc(CallHelpFunc fProc)
{
	ENTER_CRITICAL_SECTION
	F_CallHelp = fProc;
	if(F_CallHelp && !HelpCookie) {
		F_CallHelp(0, HH_INITIALIZE, (uint)&HelpCookie);
	}
	LEAVE_CRITICAL_SECTION
	return 1;
}

#endif // } 0 @v9.1.2 replaced by SetExtraProcBlock()

int SlSession::CallHelp(void * hWnd, uint cmd, uint ctx)
{
	return ExtraProcBlk.F_CallHelp ? ExtraProcBlk.F_CallHelp(hWnd, cmd, ctx) : 0;
}

int SlSession::SubstString(const char * pSrcStr, int ansiCoding, SString & rBuf)
{
	int    ok = -1;
	if(pSrcStr && pSrcStr[0] == '@' && !sstrchr(pSrcStr, ' ')) {
		SString _text(pSrcStr);
		if(LoadString_(_text.ShiftLeft(1), rBuf) > 0) {
			if(ansiCoding)
				rBuf.Transf(CTRANSF_INNER_TO_OUTER);
			ok = 1;
		}
	}
	return ok;
}

int FASTCALL SlSession::SetCodepage(SCodepage cp)
{
	int    ok = 1;
	SlThreadLocalArea & r_tla = GetTLA();
	r_tla.CurrentCp = cp;
	SString cp_text;
	if(r_tla.CurrentCp.ToStr(SCodepageIdent::fmtCLibLocale, cp_text)) {
		setlocale(LC_CTYPE, cp_text);
		setlocale(LC_COLLATE, cp_text);
	}
	return ok;
}

SCodepage SlSession::GetCodepage() const
{
	const SlThreadLocalArea & r_tla = GetConstTLA();
	return r_tla.CurrentCp;
}
//
//
//
// @v10.5.6 uint32 FASTCALL RSHash(const void * pData, size_t len); // @prototype
void __LinkFile_HASHFUNC()
{
	SlHash::RS("", 0);
}

#pragma warning(disable:4073)
#pragma init_seg(lib)
SlSession SLS; // @global
DbSession DBS; // @global

