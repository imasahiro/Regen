#pragma once
/*
	string function for SSE4.2
	@NOTE
	all functions in this header will access max 16 bytes beyond the end of input string

	@author herumi
	@note modified new BSD license
	http://opensource.org/licenses/BSD-3-Clause
*/
#include <stdlib.h>
#include <xbyak/xbyak.h>
#include <xbyak/xbyak_util.h>

namespace str_util_impl {

const size_t strstrOffset = 0;
const size_t strlenOffset = strstrOffset + 96;
const size_t strchrOffset = strlenOffset + 48;
const size_t strchr_anyOffset = strchrOffset + 48;
const size_t strchr_rangeOffset = strchr_anyOffset + 48;
const size_t findCharOffset = strchr_rangeOffset + 64;
const size_t findChar_anyOffset = findCharOffset + 64;
const size_t findChar_rangeOffset = findChar_anyOffset + 64;
const size_t findStrOffset = findChar_rangeOffset + 64;

struct StringCode : Xbyak::CodeGenerator {
	const Xbyak::util::Cpu cpu;
	StringCode(char *buf, size_t size)
		try
		: Xbyak::CodeGenerator(size, buf)
	{
		Xbyak::CodeArray::protect(buf, size, true);
		if (!cpu.has(Xbyak::util::Cpu::tSSE42)) {
			return;
		}
		/* this check is adhoc */
		const bool isSandyBridge = cpu.has(Xbyak::util::Cpu::tAVX);

		gen_strstr(isSandyBridge);
		printf("strstr size=%d\n", (int)getSize());

		nextOffset(strlenOffset);
		gen_strlen();
		printf("strlen size=%d\n", (int)(getSize() - strlenOffset));

		nextOffset(strchrOffset);
		gen_strchr(M_one);
		printf("strchr size=%d\n", (int)(getSize() - strchrOffset));

		nextOffset(strchr_anyOffset);
		gen_strchr(M_any);
		printf("strchr_any size=%d\n", (int)(getSize() - strchr_anyOffset));

		nextOffset(strchr_rangeOffset);
		gen_strchr(M_range);
		printf("strchr_range size=%d\n", (int)(getSize() - strchr_rangeOffset));

		nextOffset(findCharOffset);
		gen_findChar(M_one);
		printf("findChar size=%d\n", (int)(getSize() - findCharOffset));

		nextOffset(findChar_anyOffset);
		gen_findChar(M_any);
		printf("findChar_any size=%d\n", (int)(getSize() - findChar_anyOffset));

		nextOffset(findChar_rangeOffset);
		gen_findChar(M_range);
		printf("findChar_range size=%d\n", (int)(getSize() - findChar_rangeOffset));

		nextOffset(findStrOffset);
		gen_findStr(isSandyBridge);
		printf("findStr size=%d\n", (int)(getSize() - findStrOffset));
	} catch (Xbyak::Error err) {
		printf("ERR:%s(%d)\n", Xbyak::ConvertErrorToString(err), err);
		::exit(1);
	}
private:
	enum {
		M_one, /* match one char */
		M_any, /* match any of key[0], key[1], ... */
		M_range /* match range [key[0], key[1]], [key[2], key[3]], ... */
	};
	void nextOffset(size_t pos)
	{
		size_t cur = getSize();
		if (cur >= pos) {
			fprintf(stderr, "over %d %d\n", (int)cur, (int)pos);
			::exit(1);
		}
		while (cur < pos) {
			nop();
			cur++;
		}
	}
	// char *strstr(str, key)
	void gen_strstr(bool isSandyBridge)
	{
		inLocalLabel();
		using namespace Xbyak;
#ifdef XBYAK64
	#ifdef XBYAK64_WIN
		const Reg64& key = rdx; // 2nd
		const Reg64& save_a = r11;
		const Reg64& save_key = r9;
		mov(rax, rcx); // 1st:str
	#else
		const Reg64& key = rsi; // 2nd
		const Reg64& save_a = r8;
		const Reg64& save_key = r9;
		mov(rax, rdi); // 1st:str
	#endif
		const Reg64& c = rcx;
		const Reg64& a = rax;
#else
		const Reg32& key = edx;
		const Reg32& save_a = esi;
		const Reg32& save_key = edi;
		const Reg32& c = ecx;
		const Reg32& a = eax;
		const int P_ = 4 * 2;
		sub(esp, P_);
		mov(ptr [esp + 0], esi);
		mov(ptr [esp + 4], edi);
		mov(eax, ptr [esp + P_ + 4]);
		mov(key, ptr [esp + P_ + 8]);
#endif

		/*
			strstr(a, key);
			input key
			use save_a, save_key, c
		*/
		movdqu(xm0, ptr [key]); // xm0 = *key
	L(".lp");
		pcmpistri(xmm0, ptr [a], 12); // 12(1100b) = [equal ordered:unsigned:byte]
		if (isSandyBridge) {
			lea(a, ptr [a + 16]);
			ja(".lp"); // if (CF == 0 and ZF = 0) goto .lp
		} else {
			jbe(".headCmp"); //  if (CF == 1 or ZF == 1) goto .headCmp
			add(a, 16);
			jmp(".lp");
	L(".headCmp");
		}
		jnc(".notFound");
		// get position
		if (isSandyBridge) {
			lea(a, ptr [a + c - 16]);
		} else {
			add(a, c);
		}
		mov(save_a, a); // save a
		mov(save_key, key); // save key
	L(".tailCmp");
		movdqu(xm1, ptr [save_key]);
		pcmpistri(xmm1, ptr [save_a], 12);
		jno(".next"); // if (OF == 0) goto .next
		js(".found"); // if (SF == 1) goto .found
		// rare case
		add(save_a, 16);
		add(save_key, 16);
		jmp(".tailCmp");
	L(".next");
		add(a, 1);
		jmp(".lp");
	L(".notFound");
		xor(eax, eax);
	L(".found");
#ifdef XBYAK32
		mov(esi, ptr [esp + 0]);
		mov(edi, ptr [esp + 4]);
		add(esp, P_);
#endif
		ret();
		outLocalLabel();
	}
	void gen_strlen()
	{
		inLocalLabel();
		using namespace Xbyak;
#if defined(XBYAK64_WIN)
		const Reg64& p = rdx;
		const Reg64& c = rcx;
		const Reg64& a = rax;
		mov(rdx, rcx);
#elif defined(XBYAK64_GCC)
		const Reg64& p = rdi;
		const Reg64& c = rcx;
		const Reg64& a = rax;
#else
		const Reg32& p = edx;
		const Reg32& c = ecx;
		const Reg32& a = eax;
		mov(edx, ptr [esp + 4]);
#endif
		mov(eax, 0xff01);
		movd(xm0, eax);

		mov(a, p);
		jmp(".in");
	L("@@");
		add(a, 16);
	L(".in");
		pcmpistri(xm0, ptr [a], 0x14);
		jnz("@b");
		add(a, c);
		sub(a, p);
		ret();
		outLocalLabel();
	}
	void gen_strchr(int mode)
	{
		inLocalLabel();
		using namespace Xbyak;

#ifdef XBYAK64
	#ifdef XBYAK64_WIN
		const Reg64& p = rcx;
		const Reg64& c1 = rdx;
	#else
		const Reg64& p = rdi;
		const Reg64& c1 = rsi;
	#endif
		const Reg64& c = rcx;
		const Reg64& a = rax;
		if (mode == M_one) {
			and(c1, 0xff);
			movq(xm0, c1);
		} else {
			movdqu(xm0, ptr [c1]);
		}
		mov(a, p);
#else
		const Reg32& a = eax;
		const Reg32& c = ecx;
		if (mode == M_one) {
			movzx(eax, byte [esp + 8]);
			movd(xm0, eax);
		} else {
			mov(eax, ptr [esp + 8]);
			movdqu(xm0, ptr [eax]);
		}
		mov(a, ptr [esp + 4]);
#endif
		const int v = mode == M_range ? 4 : 0;
		jmp(".in");
	L("@@");
		add(a, 16);
	L(".in");
		pcmpistri(xm0, ptr [a], v);
		ja("@b");
		jnc(".notfound");
		add(a, c);
		ret();
	L(".notfound");
		xor(a, a);
		ret();
		outLocalLabel();
	}
	void gen_findChar(int mode)
	{
		// findChar(p(=begin), end, c)
		inLocalLabel();
		using namespace Xbyak;

#ifdef XBYAK64
	#ifdef XBYAK64_WIN
		const Reg64& p = r9;
		const Reg64& end = r10;
		if (mode == M_one) {
			movzx(r8d, r8b); // c:3rd
			movq(xm0, r8);
		} else {
			movdqu(xm0, ptr [r8]); // key:3rd
			mov(rax, r9); // keySize:4th
		}
		mov(p, rcx); // 1st
		mov(end, rdx); // 2nd
	#else
		const Reg64& p = rdi; // 1st
		const Reg64& end = rsi; // 2nd
		if (mode == M_one) {
			movzx(edx, dl); // c:3rd
			movq(xm0, rdx);
		} else {
			movdqu(xm0, ptr [rdx]); // key:3rd
			mov(rax, rcx); // keySize:4th
		}
	#endif
		const Reg64& a = rax;
		const Reg64& c = rcx;
		const Reg64& d = rdx;
#else
		const Reg32& p = esi;
		const Reg32& end = edi;
		const Reg32& a = eax;
		const Reg32& c = ecx;
		const Reg32& d = edx;
		push(esi);
		push(edi);
		const int P_ = 4 * 2;
		mov(p, ptr [esp + P_ + 4]);
		mov(end, ptr [esp + P_ + 8]);
		if (mode == M_one) {
			movzx(eax, byte [esp + P_ + 12]);
			movd(xm0, eax);
		} else {
			mov(eax, ptr [esp + P_ + 12]);
			movdqu(xm0, ptr [eax]);
			mov(eax, ptr [esp + P_ + 16]);
		}
#endif
		if (mode == M_one) {
			xor(eax, eax);
			inc(eax); // eax = 1
		}
		/*
			input  : [p, end), xm0 : char(key)
			output : a
			destroy: a, c, d, p
		*/
		const int v = mode == M_range ? 4 : 0;
		mov(d, end);
		sub(d, p); // len
		jmp(".in");
	L("@@");
		add(p, 16);
		sub(d, 16);
	L(".in");
		pcmpestri(xm0, ptr [p], v);
		ja("@b");
		jnc(".notfound");
		lea(a, ptr [p + c]);
#ifdef XBYAK32
		pop(edi);
		pop(esi);
#endif
		ret();
	L(".notfound");
		mov(a, end);
#ifdef XBYAK32
		pop(edi);
		pop(esi);
#endif
		ret();
		outLocalLabel();
	}
	// findStr(const char*begin, const char *end, const char *key, size_t keySize)
	void gen_findStr(bool /*isSandyBridge*/)
	{
		inLocalLabel();
		using namespace Xbyak;
#ifdef XBYAK64
	#ifdef XBYAK64_WIN
		const Reg64& p = r9;
		const Reg64& key = r8;
		const Reg64& save_p = r10;
		const Reg64& save_key = r11;
		const Reg64& save_a = r12;
		const Reg64& save_d = r13;
		const Address end = ptr [rsp + 24];
		mov(ptr [rsp + 8], r12);
		mov(ptr [rsp + 16], r13);
		mov(end, rdx); // save end:2nd
		mov(rax, r9); // keySize:4th
		sub(rdx, rcx); // len = end - begin(1st)
		mov(p, rcx);
	#else
		const Reg64& p = rdi; // begin::1st
		const Reg64& key = r8;
		const Reg64& save_p = r9;
		const Reg64& save_key = r10;
		const Reg64& save_a = r11;
		const Reg64& save_d = r12;
		const Reg64& end = rsi; // end::2nd
		mov(key, rdx);
		mov(rax, rcx); // keySize:4th
		mov(rdx, end);
		sub(rdx, p); // len = end - p
		push(r12);
	#endif
		const Reg64& a = rax;
		const Reg64& c = rcx;
		const Reg64& d = rdx;
#else
		const Reg32& p = esi;
		const Reg32& key = edi;
		const Reg32& save_p = ebx;
		const Reg32& save_key = ebp;
		const Reg32& a = eax;
		const Reg32& c = ecx;
		const Reg32& d = edx;
		const int P_ = 4 * 6;
		const Address save_d = ptr [esp + 16];
		const Address save_a = ptr [esp + 20];
		const Address end = ptr [esp + P_ + 8];
		sub(esp, P_);
		mov(ptr [esp + 0], esi);
		mov(ptr [esp + 4], edi);
		mov(ptr [esp + 8], ebx);
		mov(ptr [esp + 12], ebp);
		mov(p, ptr [esp + P_ + 4]); // p
		mov(edx, end); // end
		sub(edx, p); // len = end - p
		mov(key, ptr [esp + P_+ 12]); // key
		movdqu(xm0, ptr [key]);
		mov(eax, ptr [esp + P_ + 16]); // keySize
#endif

		/*
			findStr(p, end, key, keySize)
			input  : p, d:=len=end-p, xm0:key, a:keySize
			destroy: save_p, save_d, save_key, save_keySize, a, c, d
		*/
		movdqu(xm0, ptr [key]);
	L(".lp");
		pcmpestri(xmm0, ptr [p], 12); // 12(1100b) = [equal ordered:unsigned:byte]
		if (true/* isSandyBridge */) {
			lea(p, ptr [p + 16]);
			lea(d, ptr [d - 16]);
			ja(".lp"); // if (CF == 0 and ZF = 0) goto .lp
		} else {
			jbe(".headCmp"); //  if (CF == 1 or ZF == 1) goto .headCmp
			add(p, 16);
			sub(d, 16);
			jmp(".lp");
	L(".headCmp");
		}
		jnc(".notFound");
		// get position
		if (true/* isSandyBridge */) {
			lea(p, ptr [p + c - 16]);
			sub(d, c);
			add(d, 16);
		} else {
			add(p, c);
			sub(d, c);
		}
		mov(save_p, p);
		mov(save_key, key);
		mov(save_a, a);
		mov(save_d, d);
	L(".tailCmp");
		movdqu(xm1, ptr [save_key]);
		pcmpestri(xmm1, ptr [save_p], 12);
		jno(".next"); // if (OF == 0) goto .next
		js(".found"); // if (SF == 1) goto .found
		// rare case
		add(save_p, 16);
		add(save_key, 16);
		sub(a, 16);
		sub(d, 16);
		jmp(".tailCmp");
	L(".next");
		add(p, 1);
		mov(a, save_a);
#ifdef XBYAK32
		mov(edx, save_d);
		sub(edx, 1);
#else
		lea(d, ptr [save_d - 1]);
#endif
		jmp(".lp");
	L(".notFound");
		mov(a, end);
		jmp(".exit");
	L(".found");
		mov(a, p);
	L(".exit");
#ifdef XBYAK64
	#ifdef XBYAK64_WIN
		mov(r12, ptr [rsp + 8]);
		mov(r13, ptr [rsp + 16]);
	#else
		pop(r12);
	#endif
#else
		mov(esi, ptr [esp + 0]);
		mov(edi, ptr [esp + 4]);
		mov(ebx, ptr [esp + 8]);
		mov(ebp, ptr [esp + 12]);
		add(esp, P_);
#endif
		ret();
		outLocalLabel();
	}
};

template<int dummy = 0>
struct InstanceIsHere {
	static MIE_ALIGN(4096) char buf[4096];
	static StringCode code;
};

template<int dummy>
StringCode InstanceIsHere<dummy>::code(buf, sizeof(buf));

template<int dummy>
char InstanceIsHere<dummy>::buf[4096];

struct DummyCall {
	DummyCall() { InstanceIsHere<>::code.getCode(); }
};

} // str_util_impl

namespace mie {

inline bool isAvaiableSSE42()
{
	return str_util_impl::InstanceIsHere<>::code.cpu.has(Xbyak::util::Cpu::tSSE42);
}
///////////////////////////////////////////////////////////////////////////////////////
// functions like C
// const version of strstr
inline const char *strstr(const char *str, const char *key)
{
	return ((const char*(*)(const char*, const char*))(char*)str_util_impl::InstanceIsHere<>::buf)(str, key);
}

// non const version of strstr
inline char *strstr(char *str, const char *key)
{
	return ((char*(*)(char*, const char*))(char*)str_util_impl::InstanceIsHere<>::buf)(str, key);
}

// const version of strchr(c != 0)
inline const char *strchr(const char *str, int c)
{
	return ((const char*(*)(const char*, int))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::strchrOffset))(str, c);
}

// non const version of strchr(c != 0)
inline char *strchr(char *str, int c)
{
	return ((char*(*)(char*, int))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::strchrOffset))(str, c);
}

inline size_t strlen(const char *str)
{
	return ((size_t(*)(const char*))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::strlenOffset))(str);
}

/*
	find key[0] or key[1], ... in str
	@note strlen(key) <= 16, key[i] != 0
*/
inline const char *strchr_any(const char *str, const char *key)
{
	return ((const char *(*)(const char*, const char *key))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::strchr_anyOffset))(str, key);
}
inline char *strchr_any(char *str, const char *key)
{
	return ((char *(*)(char*, const char *key))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::strchr_anyOffset))(str, key);
}

/*
	find c such that key[0] <= c && c <= key[1], key[2] <= c && c <= key[3], ... in str
	@note strlen(key) <= 16, key[i] != 0
*/
inline const char *strchr_range(const char *str, const char *key)
{
	return ((const char *(*)(const char*, const char *key))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::strchr_rangeOffset))(str, key);
}
inline char *strchr_range(char *str, const char *key)
{
	return ((char *(*)(char*, const char *key))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::strchr_rangeOffset))(str, key);
}

/*
	find c in [begin, end)
	if c is not found then return end
*/
inline const char *findChar(const char *begin, const char *end, char c)
{
	return ((const char *(*)(const char*, const char *, char c))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::findCharOffset))(begin, end, c);
}
inline char *findChar(char *begin, const char *end, char c)
{
	return ((char *(*)(char*, const char *, char c))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::findCharOffset))(begin, end, c);
}

/*
	find any of key[0..keySize - 1] in [begin, end)
	if char is not found then return end
	@note keySize <= 16
*/
inline const char *findChar_any(const char *begin, const char *end, const char *key, size_t keySize)
{
	return ((const char *(*)(const char*, const char *, const char *,size_t))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::findChar_anyOffset))(begin, end, key, keySize);
}
inline char *findChar_any(char *begin, const char *end, const char *key, size_t keySize)
{
	return ((char *(*)(char*, const char *, const char *,size_t))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::findChar_anyOffset))(begin, end, key, keySize);
}

/*
	find character in range [key[0]..key[1]], [key[2]..key[3]], ... in [begin, end)
	if char is not found then return end
	@note keySize <= 16
*/
inline const char *findChar_range(const char *begin, const char *end, const char *key, size_t keySize)
{
	return ((const char *(*)(const char*, const char *, const char *,size_t))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::findChar_rangeOffset))(begin, end, key, keySize);
}
inline char *findChar_range(char *begin, const char *end, const char *key, size_t keySize)
{
	return ((char *(*)(char*, const char *, const char *,size_t))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::findChar_rangeOffset))(begin, end, key, keySize);
}
/*
	find [key, key + keySize) in [begin, end)
*/
inline const char *findStr(const char*begin, const char *end, const char *key, size_t keySize)
{
	return ((const char *(*)(const char*, const char *, const char *,size_t))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::findStrOffset))(begin, end, key, keySize);
}
inline char *findStr(char*begin, const char *end, const char *key, size_t keySize)
{
	return ((char *(*)(char*, const char *, const char *,size_t))((char*)str_util_impl::InstanceIsHere<>::buf + str_util_impl::findStrOffset))(begin, end, key, keySize);
}

} // mie
