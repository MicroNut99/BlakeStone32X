/* BlakeStone32X: bstone::FileStream over the platform file table.
   Reads come straight from the platform (cart ROM on the 32X, disk on the
   PC test build). Writes (config, saves) are collected in RAM and handed to
   the platform on close. */
#include "bstone_file_stream.h"
#include <cstring>
#include <vector>
#include "bs32x.h"

namespace bstone {

namespace {

struct Bs32xFile {
    std::string name;
    const uint8_t* rdata;
    std::vector<uint8_t> wdata;
    int64_t size;
    int64_t pos;
    bool writable;
};

Bs32xFile* ctx(void* context)
{
    return static_cast<Bs32xFile*>(context);
}

/* config and high scores live in the cart's save RAM */
int sram_region(const std::string& name)
{
    if (name.find("bstone_config") != std::string::npos) {
        return BS32X_SRAM_CONFIG;
    }
    if (name.find("_high_scores") != std::string::npos) {
        return BS32X_SRAM_SCORES;
    }
    return -1;
}

uint8_t sram_buf[0x800];    /* the last one read (one is open at a time) */

bool open_read(const std::string& name, const void** data, long* size)
{
    int region = sram_region(name);
    if (region < 0) {
        return bs32x_file_open_read(name.c_str(), data, size);
    }
    int n = bs32x_sram_read(region, sram_buf, static_cast<int>(sizeof(sram_buf)));
    if (n < 0) {
        return false;
    }
    *data = sram_buf;
    *size = n;
    return true;
}

} // namespace

FileStream::FileStream() :
    context_(),
    can_read_(),
    can_seek_(),
    can_write_()
{
}

FileStream::FileStream(
    const std::string& file_name,
    StreamOpenMode open_mode) :
    context_(),
    can_read_(),
    can_seek_(),
    can_write_()
{
    static_cast<void>(open(file_name, open_mode));
}

FileStream::~FileStream()
{
    close();
}

bool FileStream::open(
    const std::string& file_name,
    StreamOpenMode open_mode)
{
    close();

    const void* data = nullptr;
    long size = 0;
    bool found = open_read(file_name, &data, &size);

    if (open_mode == StreamOpenMode::read && !found) {
        return false;
    }

    auto f = new Bs32xFile();
    f->name = file_name;
    f->rdata = static_cast<const uint8_t*>(data);
    f->size = found ? size : 0;
    f->pos = 0;
    f->writable = (open_mode != StreamOpenMode::read);

    if (f->writable) {
        if (open_mode == StreamOpenMode::read_write && found) {
            f->wdata.assign(f->rdata, f->rdata + f->size);
        }
        f->rdata = nullptr;
    }

    context_ = f;
    can_read_ = (open_mode != StreamOpenMode::write);
    can_seek_ = true;
    can_write_ = f->writable;
    return true;
}

void FileStream::close()
{
    if (!context_) {
        return;
    }

    auto f = ctx(context_);

    if (f->writable) {
        int region = sram_region(f->name);
        if (region >= 0) {
            bs32x_sram_write(region, f->wdata.data(), static_cast<int>(f->wdata.size()));
        } else {
            bs32x_file_save(f->name.c_str(), f->wdata.data(), static_cast<long>(f->wdata.size()));
        }
    }

    delete f;
    context_ = nullptr;
    can_read_ = false;
    can_seek_ = false;
    can_write_ = false;
}

bool FileStream::is_open() const
{
    return context_ != nullptr;
}

int64_t FileStream::get_size()
{
    if (!context_) {
        return -1;
    }

    auto f = ctx(context_);
    return f->writable ? static_cast<int64_t>(f->wdata.size()) : f->size;
}

bool FileStream::set_size(
    int64_t size)
{
    if (!context_ || !ctx(context_)->writable || size < 0) {
        return false;
    }

    ctx(context_)->wdata.resize(static_cast<size_t>(size));
    return true;
}

int64_t FileStream::seek(
    int64_t offset,
    StreamSeekOrigin origin)
{
    if (!context_) {
        return -1;
    }

    auto f = ctx(context_);
    int64_t base = 0;

    switch (origin) {
    case StreamSeekOrigin::begin:
        base = 0;
        break;
    case StreamSeekOrigin::current:
        base = f->pos;
        break;
    case StreamSeekOrigin::end:
        base = get_size();
        break;
    }

    int64_t pos = base + offset;

    if (pos < 0) {
        return -1;
    }

    f->pos = pos;
    return pos;
}

int FileStream::read(
    void* buffer,
    int count)
{
    if (!context_ || count < 0) {
        return -1;
    }

    auto f = ctx(context_);
    int64_t size = get_size();
    int64_t left = size - f->pos;

    if (left <= 0) {
        return 0;
    }

    if (count > left) {
        count = static_cast<int>(left);
    }

    const uint8_t* src = f->writable ? f->wdata.data() : f->rdata;
    std::memcpy(buffer, src + f->pos, static_cast<size_t>(count));
    f->pos += count;
    return count;
}

bool FileStream::write(
    const void* buffer,
    int count)
{
    if (!context_ || !ctx(context_)->writable || count < 0) {
        return false;
    }

    auto f = ctx(context_);
    size_t end = static_cast<size_t>(f->pos) + static_cast<size_t>(count);

    if (end > f->wdata.size()) {
        f->wdata.resize(end);
    }

    std::memcpy(f->wdata.data() + f->pos, buffer, static_cast<size_t>(count));
    f->pos += count;
    return true;
}

bool FileStream::can_read() const
{
    return can_read_;
}

bool FileStream::can_seek() const
{
    return can_seek_;
}

bool FileStream::can_write() const
{
    return can_write_;
}

// (static)
bool FileStream::is_exists(
    const std::string& file_name)
{
    const void* data = nullptr;
    long size = 0;
    return open_read(file_name, &data, &size);
}

const uint8_t* FileStream::rom_data() const
{
    if (!context_) {
        return nullptr;
    }

    auto f = static_cast<Bs32xFile*>(context_);
    return f->writable ? nullptr : f->rdata;
}

} // bstone
