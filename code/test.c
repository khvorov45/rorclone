#include "common.c"

static void test_arena() {
    u8 buf[64];
    Arena arena_ = {.base = buf, .size = sizeof(buf)};
    assert(arena_.used == 0);
    assert(arena_.tempCount == 0);
    Arena* arena = &arena_;

    // NOTE: arenaAllocBytes, arenaFreeptr, arenaFreesize
    tempMemoryBlock(arena){
        assert(arena->used == 0);

        void* ptr = arenaAllocBytes(arena, 16);
        assert(ptr == arena->base);
        assert(arena->used == 16);
        assert(arenaFreesize(arena) == arena->size - 16);
        assert(arenaFreeptr(arena) == arena->base + 16);

        assert(arena->used == 16);
        ptr = arenaAllocBytes(arena, 32);
        assert(ptr == arena->base + 16);
        assert(arena->used == 48);
        assert(arenaFreesize(arena) == arena->size - 48);
        assert(arenaFreeptr(arena) == arena->base + 48);
    }

    // NOTE: arenaAllocOne, arenaAllocAndZeroOne
    tempMemoryBlock(arena) {
        assert(arena->used == 0);

        tempMemoryBlock(arena) {
            Str* str = arenaAllocOne(arena, Str);
            assert((u64)str == (u64)arena->base);
            assert(arena->used == sizeof(Str));
            *str = STR("test");
        }

        tempMemoryBlock(arena) {
            Str* str = arenaAllocOne(arena, Str);
            assert((u64)str == (u64)arena->base);
            assert(arena->used == sizeof(Str));
            assert(streq(*str, STR("test")));
        }

        Str* str = arenaAllocAndZeroOne(arena, Str);
        assert((u64)str == (u64)arena->base);
        assert(arena->used == sizeof(Str));
        assert(str->ptr == 0);
        assert(str->len == 0);
    }

    // NOTE: arenaAllocArray
    tempMemoryBlock(arena) {
        assert(arena->used == 0);
        memset(arena->base, 0xFF, sizeof(Str) * 3);

        Strs strs = arenaAllocArray(arena, Str, 3);
        assert(strs.ptr == arena->base);
        assert(strs.len == 3);
        assert(arena->used == sizeof(Str) * 3);

        for (i64 index = 0; index < 3; index++) {
            assert(strs.ptr[index].ptr != 0);
            assert(strs.ptr[index].len != 0);
        }
    }


    // NOTE: arenaAllocAndZeroArray
    tempMemoryBlock(arena) {
        assert(arena->used == 0);
        memset(arena->base, 0xFF, sizeof(Str) * 3);

        Strs strs = arenaAllocAndZeroArray(arena, Str, 3);
        assert(strs.ptr == arena->base);
        assert(strs.len == 3);
        assert(arena->used == sizeof(Str) * 3);

        for (i64 index = 0; index < 3; index++) {
            assert(strs.ptr[index].ptr == 0);
            assert(strs.ptr[index].len == 0);
        }
    }

    // NOTE: arenaAllocDynarr
    tempMemoryBlock(arena) {
        assert(arena->used == 0);
        memset(arena->base, 0xFF, sizeof(Str) * 3);

        struct {Str* ptr; i64 len; i64 cap;} strs = arenaAllocDynarr(arena, Str, 3);
        assert(strs.ptr == arena->base);
        assert(strs.len == 0);
        assert(strs.cap == 3);
        assert(arena->used == sizeof(Str) * 3);

        for (i64 index = 0; index < 3; index++) {
            assert(strs.ptr[index].ptr != 0);
            assert(strs.ptr[index].len != 0);
        }
    }

    // NOTE: arenaAllocAndZeroDynarr
    tempMemoryBlock(arena) {
        assert(arena->used == 0);
        memset(arena->base, 0xFF, sizeof(Str) * 3);

        struct {Str* ptr; i64 len; i64 cap;} strs = arenaAllocAndZeroDynarr(arena, Str, 3);
        assert(strs.ptr == arena->base);
        assert(strs.len == 0);
        assert(strs.cap == 3);
        assert(arena->used == sizeof(Str) * 3);

        for (i64 index = 0; index < 3; index++) {
            assert(strs.ptr[index].ptr == 0);
            assert(strs.ptr[index].len == 0);
        }
    }

    // NOTE: temp memory
    {
        TempMemory temp1 = beginTempMemory(arena);
            assert(temp1.arena == arena);
            assert(temp1.usedBefore == 0);
            assert(temp1.tempBefore == 0);
            assert(arena->used == 0);
            assert(arena->tempCount == 1);
            arenaAllocBytes(arena, 16);
            assert(arena->used == 16);

            TempMemory temp2 = beginTempMemory(arena);
                assert(temp2.arena == arena);
                assert(temp2.usedBefore == 16);
                assert(temp2.tempBefore == 1);
                assert(arena->used == 16);
                assert(arena->tempCount == 2);
                arenaAllocBytes(arena, 32);
                assert(arena->used == 48);
                assert(arena->tempCount == 2);
            endTempMemory(&temp2);

            assert(arena->used == 16);
            assert(arena->tempCount == 1);
        endTempMemory(&temp1);

        assert(arena->used == 0);
        assert(arena->tempCount == 0);

        assert(temp1.arena == 0);
        assert(temp1.usedBefore == 0);
        assert(temp1.tempBefore == 0);
        assert(temp2.arena == 0);
        assert(temp2.usedBefore == 0);
        assert(temp2.tempBefore == 0);

        tempMemoryBlock(arena) {
            arenaAllocBytes(arena, 16);
            assert(arena->used == 16);
            assert(arena->tempCount == 1);
            tempMemoryBlock(arena) {
                arenaAllocBytes(arena, 32);
                assert(arena->used == 48);
                assert(arena->tempCount == 2);
            }
            assert(arena->used == 16);
            assert(arena->tempCount == 1);
        }
        assert(arena->used == 0);
        assert(arena->tempCount == 0);
    }
}

typedef void (*OutputProc)(Str str);

static void test(OutputProc outputProc) {
    #define runtest(name) outputProc(STR(STRINGIFY(name))); test_##name(); outputProc(STR(" passed\n"));
    runtest(arena);
}
