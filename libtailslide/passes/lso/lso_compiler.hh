#pragma once

#include <vector>

#include "lslmini.hh"
#include "visitor.hh"
#include "bitstream.hh"

namespace Tailslide {

/// LSO-specific bitstream with LSO-specific serialization helpers
class LSOBitStream : public BitStream {
  public:
    explicit LSOBitStream(Endianness endian=ENDIAN_BIG) : BitStream(endian) {}
    LSOBitStream(const uint8_t *data, const uint32_t length, Endianness endian=ENDIAN_BIG): BitStream(data, length, endian) {}
    LSOBitStream(LSOBitStream &&other) noexcept: BitStream(std::move(other)) {}
    LSOBitStream(const LSOBitStream &other) = delete;
};


inline LSOBitStream& operator<<(LSOBitStream &bs, const Vector3 &v) {
  bs << v.z;
  bs << v.y;
  bs << v.x;
  return bs;
}

inline LSOBitStream& operator>>(LSOBitStream &bs, Vector3 &v) {
  bs >> v.z;
  bs >> v.y;
  bs >> v.x;
  return bs;
}

inline LSOBitStream& operator<<(LSOBitStream &bs, const Quaternion &v) {
  bs << v.s;
  bs << v.z;
  bs << v.y;
  bs << v.x;
  return bs;
}

inline LSOBitStream& operator>>(LSOBitStream &bs, Quaternion &v) {
  bs >> v.s;
  bs >> v.z;
  bs >> v.y;
  bs >> v.x;
  return bs;
}


class LSOHeapManager {
  public:
    uint32_t writeConstant(LSLConstant *constant);
    LSOBitStream heap_bs {ENDIAN_BIG};
  protected:
    void writeHeader(uint32_t size, LST_TYPE type);
};

class LSOCompilerVisitor : public ASTVisitor {
  public:
    LSOBitStream script_bs {ENDIAN_BIG};
  protected:
    virtual bool visit(LSLScript *node);
    LSOBitStream globals_bs {ENDIAN_BIG};
    LSOBitStream functions_bs {ENDIAN_BIG};
    LSOBitStream states_bs {ENDIAN_BIG};
    LSOHeapManager heap_manager;
};

}
