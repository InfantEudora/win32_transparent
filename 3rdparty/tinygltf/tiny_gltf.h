#ifndef TINY_GLTF_HEADER_H
#define TINY_GLTF_HEADER_H

#define TINYGLTF_NO_STB_IMAGE_WRITE

#include <array>
#include <cassert>
#include <cmath>  // std::fabs
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Auto-detect C++14 standard version
#if !defined(TINYGLTF_USE_CPP14) && defined(__cplusplus) && \
    (__cplusplus >= 201402L)
#define TINYGLTF_USE_CPP14
#endif

#ifdef __ANDROID__
#ifdef TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#include <android/asset_manager.h>
#endif
#endif

#ifdef __GNUC__
#if (__GNUC__ < 4) || ((__GNUC__ == 4) && (__GNUC_MINOR__ <= 8))
#define TINYGLTF_NOEXCEPT
#else
#define TINYGLTF_NOEXCEPT noexcept
#endif
#else
#define TINYGLTF_NOEXCEPT noexcept
#endif

#define DEFAULT_METHODS(x)             \
  ~x() = default;                      \
  x(const x &) = default;              \
  x(x &&) TINYGLTF_NOEXCEPT = default; \
  x &operator=(const x &) = default;   \
  x &operator=(x &&) TINYGLTF_NOEXCEPT = default;

namespace tinygltf {

#define TINYGLTF_MODE_POINTS (0)
#define TINYGLTF_MODE_LINE (1)
#define TINYGLTF_MODE_LINE_LOOP (2)
#define TINYGLTF_MODE_LINE_STRIP (3)
#define TINYGLTF_MODE_TRIANGLES (4)
#define TINYGLTF_MODE_TRIANGLE_STRIP (5)
#define TINYGLTF_MODE_TRIANGLE_FAN (6)

#define TINYGLTF_COMPONENT_TYPE_BYTE (5120)
#define TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE (5121)
#define TINYGLTF_COMPONENT_TYPE_SHORT (5122)
#define TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT (5123)
#define TINYGLTF_COMPONENT_TYPE_INT (5124)
#define TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT (5125)
#define TINYGLTF_COMPONENT_TYPE_FLOAT (5126)
#define TINYGLTF_COMPONENT_TYPE_DOUBLE \
  (5130)  // OpenGL double type. Note that some of glTF 2.0 validator does not
          // support double type even the schema seems allow any value of
          // integer:
          // https://github.com/KhronosGroup/glTF/blob/b9884a2fd45130b4d673dd6c8a706ee21ee5c5f7/specification/2.0/schema/accessor.schema.json#L22

#define TINYGLTF_TEXTURE_FILTER_NEAREST (9728)
#define TINYGLTF_TEXTURE_FILTER_LINEAR (9729)
#define TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_NEAREST (9984)
#define TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_NEAREST (9985)
#define TINYGLTF_TEXTURE_FILTER_NEAREST_MIPMAP_LINEAR (9986)
#define TINYGLTF_TEXTURE_FILTER_LINEAR_MIPMAP_LINEAR (9987)

#define TINYGLTF_TEXTURE_WRAP_REPEAT (10497)
#define TINYGLTF_TEXTURE_WRAP_CLAMP_TO_EDGE (33071)
#define TINYGLTF_TEXTURE_WRAP_MIRRORED_REPEAT (33648)

// Redeclarations of the above for technique.parameters.
#define TINYGLTF_PARAMETER_TYPE_BYTE (5120)
#define TINYGLTF_PARAMETER_TYPE_UNSIGNED_BYTE (5121)
#define TINYGLTF_PARAMETER_TYPE_SHORT (5122)
#define TINYGLTF_PARAMETER_TYPE_UNSIGNED_SHORT (5123)
#define TINYGLTF_PARAMETER_TYPE_INT (5124)
#define TINYGLTF_PARAMETER_TYPE_UNSIGNED_INT (5125)
#define TINYGLTF_PARAMETER_TYPE_FLOAT (5126)

#define TINYGLTF_PARAMETER_TYPE_FLOAT_VEC2 (35664)
#define TINYGLTF_PARAMETER_TYPE_FLOAT_VEC3 (35665)
#define TINYGLTF_PARAMETER_TYPE_FLOAT_VEC4 (35666)

#define TINYGLTF_PARAMETER_TYPE_INT_VEC2 (35667)
#define TINYGLTF_PARAMETER_TYPE_INT_VEC3 (35668)
#define TINYGLTF_PARAMETER_TYPE_INT_VEC4 (35669)

#define TINYGLTF_PARAMETER_TYPE_BOOL (35670)
#define TINYGLTF_PARAMETER_TYPE_BOOL_VEC2 (35671)
#define TINYGLTF_PARAMETER_TYPE_BOOL_VEC3 (35672)
#define TINYGLTF_PARAMETER_TYPE_BOOL_VEC4 (35673)

#define TINYGLTF_PARAMETER_TYPE_FLOAT_MAT2 (35674)
#define TINYGLTF_PARAMETER_TYPE_FLOAT_MAT3 (35675)
#define TINYGLTF_PARAMETER_TYPE_FLOAT_MAT4 (35676)

#define TINYGLTF_PARAMETER_TYPE_SAMPLER_2D (35678)

// End parameter types

#define TINYGLTF_TYPE_VEC2 (2)
#define TINYGLTF_TYPE_VEC3 (3)
#define TINYGLTF_TYPE_VEC4 (4)
#define TINYGLTF_TYPE_MAT2 (32 + 2)
#define TINYGLTF_TYPE_MAT3 (32 + 3)
#define TINYGLTF_TYPE_MAT4 (32 + 4)
#define TINYGLTF_TYPE_SCALAR (64 + 1)
#define TINYGLTF_TYPE_VECTOR (64 + 4)
#define TINYGLTF_TYPE_MATRIX (64 + 16)

#define TINYGLTF_IMAGE_FORMAT_JPEG (0)
#define TINYGLTF_IMAGE_FORMAT_PNG (1)
#define TINYGLTF_IMAGE_FORMAT_BMP (2)
#define TINYGLTF_IMAGE_FORMAT_GIF (3)

#define TINYGLTF_TEXTURE_FORMAT_ALPHA (6406)
#define TINYGLTF_TEXTURE_FORMAT_RGB (6407)
#define TINYGLTF_TEXTURE_FORMAT_RGBA (6408)
#define TINYGLTF_TEXTURE_FORMAT_LUMINANCE (6409)
#define TINYGLTF_TEXTURE_FORMAT_LUMINANCE_ALPHA (6410)

#define TINYGLTF_TEXTURE_TARGET_TEXTURE2D (3553)
#define TINYGLTF_TEXTURE_TYPE_UNSIGNED_BYTE (5121)

#define TINYGLTF_TARGET_ARRAY_BUFFER (34962)
#define TINYGLTF_TARGET_ELEMENT_ARRAY_BUFFER (34963)

#define TINYGLTF_SHADER_TYPE_VERTEX_SHADER (35633)
#define TINYGLTF_SHADER_TYPE_FRAGMENT_SHADER (35632)

#define TINYGLTF_DOUBLE_EPS (1.e-12)
#define TINYGLTF_DOUBLE_EQUAL(a, b) (std::fabs((b) - (a)) < TINYGLTF_DOUBLE_EPS)

#ifdef __ANDROID__
#ifdef TINYGLTF_ANDROID_LOAD_FROM_ASSETS
#ifdef TINYGLTF_IMPLEMENTATION
AAssetManager *asset_manager = nullptr;
#else
extern AAssetManager *asset_manager;
#endif
#endif
#endif

typedef enum {
  NULL_TYPE,
  REAL_TYPE,
  INT_TYPE,
  BOOL_TYPE,
  STRING_TYPE,
  ARRAY_TYPE,
  BINARY_TYPE,
  OBJECT_TYPE
} Type;

typedef enum {
  Permissive,
  Strict
} ParseStrictness;

static inline int32_t GetComponentSizeInBytes(uint32_t componentType) {
  if (componentType == TINYGLTF_COMPONENT_TYPE_BYTE) {
    return 1;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
    return 1;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_SHORT) {
    return 2;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
    return 2;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_INT) {
    return 4;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
    return 4;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_FLOAT) {
    return 4;
  } else if (componentType == TINYGLTF_COMPONENT_TYPE_DOUBLE) {
    return 8;
  } else {
    // Unknown component type
    return -1;
  }
}

static inline int32_t GetNumComponentsInType(uint32_t ty) {
  if (ty == TINYGLTF_TYPE_SCALAR) {
    return 1;
  } else if (ty == TINYGLTF_TYPE_VEC2) {
    return 2;
  } else if (ty == TINYGLTF_TYPE_VEC3) {
    return 3;
  } else if (ty == TINYGLTF_TYPE_VEC4) {
    return 4;
  } else if (ty == TINYGLTF_TYPE_MAT2) {
    return 4;
  } else if (ty == TINYGLTF_TYPE_MAT3) {
    return 9;
  } else if (ty == TINYGLTF_TYPE_MAT4) {
    return 16;
  } else {
    // Unknown component type
    return -1;
  }
}

// TODO(syoyo): Move these functions to TinyGLTF class
bool IsDataURI(const std::string &in);
bool DecodeDataURI(std::vector<unsigned char> *out, std::string &mime_type,
                   const std::string &in, size_t reqBytes, bool checkSize);

#ifdef __clang__
#pragma clang diagnostic push
// Suppress warning for : static Value null_value
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#pragma clang diagnostic ignored "-Wpadded"
#endif

// Simple class to represent JSON object
class Value {
 public:
  typedef std::vector<Value> Array;
  typedef std::map<std::string, Value> Object;

  Value() = default;

  explicit Value(bool b) : type_(BOOL_TYPE) { boolean_value_ = b; }
  explicit Value(int i) : type_(INT_TYPE) {
    int_value_ = i;
    real_value_ = i;
  }
  explicit Value(double n) : type_(REAL_TYPE) { real_value_ = n; }
  explicit Value(const std::string &s) : type_(STRING_TYPE) {
    string_value_ = s;
  }
  explicit Value(std::string &&s)
      : type_(STRING_TYPE), string_value_(std::move(s)) {}
  explicit Value(const char *s) : type_(STRING_TYPE) { string_value_ = s; }
  explicit Value(const unsigned char *p, size_t n) : type_(BINARY_TYPE) {
    binary_value_.resize(n);
    memcpy(binary_value_.data(), p, n);
  }
  explicit Value(std::vector<unsigned char> &&v) noexcept
      : type_(BINARY_TYPE),
        binary_value_(std::move(v)) {}
  explicit Value(const Array &a) : type_(ARRAY_TYPE) { array_value_ = a; }
  explicit Value(Array &&a) noexcept : type_(ARRAY_TYPE),
                                       array_value_(std::move(a)) {}

  explicit Value(const Object &o) : type_(OBJECT_TYPE) { object_value_ = o; }
  explicit Value(Object &&o) noexcept : type_(OBJECT_TYPE),
                                        object_value_(std::move(o)) {}

  DEFAULT_METHODS(Value)

  char Type() const { return static_cast<char>(type_); }

  bool IsBool() const { return (type_ == BOOL_TYPE); }

  bool IsInt() const { return (type_ == INT_TYPE); }

  bool IsNumber() const { return (type_ == REAL_TYPE) || (type_ == INT_TYPE); }

  bool IsReal() const { return (type_ == REAL_TYPE); }

  bool IsString() const { return (type_ == STRING_TYPE); }

  bool IsBinary() const { return (type_ == BINARY_TYPE); }

  bool IsArray() const { return (type_ == ARRAY_TYPE); }

  bool IsObject() const { return (type_ == OBJECT_TYPE); }

  // Use this function if you want to have number value as double.
  double GetNumberAsDouble() const {
    if (type_ == INT_TYPE) {
      return double(int_value_);
    } else {
      return real_value_;
    }
  }

  // Use this function if you want to have number value as int.
  // TODO(syoyo): Support int value larger than 32 bits
  int GetNumberAsInt() const {
    if (type_ == REAL_TYPE) {
      return int(real_value_);
    } else {
      return int_value_;
    }
  }

  // Accessor
  template <typename T>
  const T &Get() const;
  template <typename T>
  T &Get();

  // Lookup value from an array
  const Value &Get(size_t idx) const {
    static Value null_value;
    assert(IsArray());
    return (idx < array_value_.size())
               ? array_value_[idx]
               : null_value;
  }

  // Lookup value from a key-value pair
  const Value &Get(const std::string &key) const {
    static Value null_value;
    assert(IsObject());
    Object::const_iterator it = object_value_.find(key);
    return (it != object_value_.end()) ? it->second : null_value;
  }

  size_t ArrayLen() const {
    if (!IsArray()) return 0;
    return array_value_.size();
  }

  // Valid only for object type.
  bool Has(const std::string &key) const {
    if (!IsObject()) return false;
    Object::const_iterator it = object_value_.find(key);
    return (it != object_value_.end()) ? true : false;
  }

  // List keys
  std::vector<std::string> Keys() const {
    std::vector<std::string> keys;
    if (!IsObject()) return keys;  // empty

    for (Object::const_iterator it = object_value_.begin();
         it != object_value_.end(); ++it) {
      keys.push_back(it->first);
    }

    return keys;
  }

  size_t Size() const { return (IsArray() ? ArrayLen() : Keys().size()); }

  bool operator==(const tinygltf::Value &other) const;

 protected:
  int type_ = NULL_TYPE;

  int int_value_ = 0;
  double real_value_ = 0.0;
  std::string string_value_;
  std::vector<unsigned char> binary_value_;
  Array array_value_;
  Object object_value_;
  bool boolean_value_ = false;
};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#define TINYGLTF_VALUE_GET(ctype, var)            \
  template <>                                     \
  inline const ctype &Value::Get<ctype>() const { \
    return var;                                   \
  }                                               \
  template <>                                     \
  inline ctype &Value::Get<ctype>() {             \
    return var;                                   \
  }
TINYGLTF_VALUE_GET(bool, boolean_value_)
TINYGLTF_VALUE_GET(double, real_value_)
TINYGLTF_VALUE_GET(int, int_value_)
TINYGLTF_VALUE_GET(std::string, string_value_)
TINYGLTF_VALUE_GET(std::vector<unsigned char>, binary_value_)
TINYGLTF_VALUE_GET(Value::Array, array_value_)
TINYGLTF_VALUE_GET(Value::Object, object_value_)
#undef TINYGLTF_VALUE_GET

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++98-compat"
#pragma clang diagnostic ignored "-Wpadded"
#endif

/// Aggregate object for representing a color
using ColorValue = std::array<double, 4>;

// === legacy interface ====
// TODO(syoyo): Deprecate `Parameter` class.
struct Parameter {
  bool bool_value = false;
  bool has_number_value = false;
  std::string string_value;
  std::vector<double> number_array;
  std::map<std::string, double> json_double_value;
  double number_value = 0.0;

  // context sensitive methods. depending the type of the Parameter you are
  // accessing, these are either valid or not
  // If this parameter represent a texture map in a material, will return the
  // texture index

  /// Return the index of a texture if this Parameter is a texture map.
  /// Returned value is only valid if the parameter represent a texture from a
  /// material
  int TextureIndex() const {
    const auto it = json_double_value.find("index");
    if (it != std::end(json_double_value)) {
      return int(it->second);
    }
    return -1;
  }

  /// Return the index of a texture coordinate set if this Parameter is a
  /// texture map. Returned value is only valid if the parameter represent a
  /// texture from a material
  int TextureTexCoord() const {
    const auto it = json_double_value.find("texCoord");
    if (it != std::end(json_double_value)) {
      return int(it->second);
    }
    // As per the spec, if texCoord is omitted, this parameter is 0
    return 0;
  }

  /// Return the scale of a texture if this Parameter is a normal texture map.
  /// Returned value is only valid if the parameter represent a normal texture
  /// from a material
  double TextureScale() const {
    const auto it = json_double_value.find("scale");
    if (it != std::end(json_double_value)) {
      return it->second;
    }
    // As per the spec, if scale is omitted, this parameter is 1
    return 1;
  }

  /// Return the strength of a texture if this Parameter is a an occlusion map.
  /// Returned value is only valid if the parameter represent an occlusion map
  /// from a material
  double TextureStrength() const {
    const auto it = json_double_value.find("strength");
    if (it != std::end(json_double_value)) {
      return it->second;
    }
    // As per the spec, if strength is omitted, this parameter is 1
    return 1;
  }

  /// Material factor, like the roughness or metalness of a material
  /// Returned value is only valid if the parameter represent a texture from a
  /// material
  double Factor() const { return number_value; }

  /// Return the color of a material
  /// Returned value is only valid if the parameter represent a texture from a
  /// material
  ColorValue ColorFactor() const {
    return {
        {// this aggregate initialize the std::array object, and uses C++11 RVO.
         number_array[0], number_array[1], number_array[2],
         (number_array.size() > 3 ? number_array[3] : 1.0)}};
  }

  Parameter() = default;
  DEFAULT_METHODS(Parameter)
  bool operator==(const Parameter &) const;
};

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#endif

typedef std::map<std::string, Parameter> ParameterMap;
typedef std::map<std::string, Value> ExtensionMap;

struct AnimationChannel {
  int sampler{-1};          // required
  int target_node{-1};      // optional index of the node to target (alternative
                            // target should be provided by extension)
  std::string target_path;  // required with standard values of ["translation",
                            // "rotation", "scale", "weights"]
  Value extras;
  ExtensionMap extensions;
  Value target_extras;
  ExtensionMap target_extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
  std::string target_extras_json_string;
  std::string target_extensions_json_string;

  AnimationChannel() = default;
  DEFAULT_METHODS(AnimationChannel)
  bool operator==(const AnimationChannel &) const;
};

struct AnimationSampler {
  int input{-1};              // required
  int output{-1};             // required
  std::string interpolation;  // "LINEAR", "STEP","CUBICSPLINE" or user defined
                              // string. default "LINEAR"
  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  AnimationSampler() : interpolation("LINEAR") {}
  DEFAULT_METHODS(AnimationSampler)
  bool operator==(const AnimationSampler &) const;
};

struct Animation {
  std::string name;
  std::vector<AnimationChannel> channels;
  std::vector<AnimationSampler> samplers;
  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  Animation() = default;
  DEFAULT_METHODS(Animation)
  bool operator==(const Animation &) const;
};

struct Skin {
  std::string name;
  int inverseBindMatrices{-1};  // required here but not in the spec
  int skeleton{-1};             // The index of the node used as a skeleton root
  std::vector<int> joints;      // Indices of skeleton nodes

  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  Skin() = default;
  DEFAULT_METHODS(Skin)
  bool operator==(const Skin &) const;
};

struct Sampler {
  std::string name;
  // glTF 2.0 spec does not define default value for `minFilter` and
  // `magFilter`. Set -1 in TinyGLTF(issue #186)
  int minFilter =
      -1;  // optional. -1 = no filter defined. ["NEAREST", "LINEAR",
           // "NEAREST_MIPMAP_NEAREST", "LINEAR_MIPMAP_NEAREST",
           // "NEAREST_MIPMAP_LINEAR", "LINEAR_MIPMAP_LINEAR"]
  int magFilter =
      -1;  // optional. -1 = no filter defined. ["NEAREST", "LINEAR"]
  int wrapS =
      TINYGLTF_TEXTURE_WRAP_REPEAT;  // ["CLAMP_TO_EDGE", "MIRRORED_REPEAT",
                                     // "REPEAT"], default "REPEAT"
  int wrapT =
      TINYGLTF_TEXTURE_WRAP_REPEAT;  // ["CLAMP_TO_EDGE", "MIRRORED_REPEAT",
                                     // "REPEAT"], default "REPEAT"
  // int wrapR = TINYGLTF_TEXTURE_WRAP_REPEAT;  // TinyGLTF extension. currently
  // not used.

  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  Sampler() = default;
  DEFAULT_METHODS(Sampler)
  bool operator==(const Sampler &) const;
};

struct Image {
  std::string name;
  int width{-1};
  int height{-1};
  int component{-1};
  int bits{-1};        // bit depth per channel. 8(byte), 16 or 32.
  int pixel_type{-1};  // pixel type(TINYGLTF_COMPONENT_TYPE_***). usually
                       // UBYTE(bits = 8) or USHORT(bits = 16)
  std::vector<unsigned char> image;
  int bufferView{-1};    // (required if no uri)
  std::string mimeType;  // (required if no uri) ["image/jpeg", "image/png",
                         // "image/bmp", "image/gif"]
  std::string uri;       // (required if no mimeType) uri is not decoded(e.g.
                         // whitespace may be represented as %20)
  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  // When this flag is true, data is stored to `image` in as-is format(e.g. jpeg
  // compressed for "image/jpeg" mime) This feature is good if you use custom
  // image loader function. (e.g. delayed decoding of images for faster glTF
  // parsing).
  bool as_is{false};

  Image() = default;
  DEFAULT_METHODS(Image)

  bool operator==(const Image &) const;
};

struct Texture {
  std::string name;

  int sampler{-1};
  int source{-1};
  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  Texture() = default;
  DEFAULT_METHODS(Texture)

  bool operator==(const Texture &) const;
};

struct TextureInfo {
  int index{-1};    // required.
  int texCoord{0};  // The set index of texture's TEXCOORD attribute used for
                    // texture coordinate mapping.

  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  TextureInfo() = default;
  DEFAULT_METHODS(TextureInfo)
  bool operator==(const TextureInfo &) const;
};

struct NormalTextureInfo {
  int index{-1};    // required
  int texCoord{0};  // The set index of texture's TEXCOORD attribute used for
                    // texture coordinate mapping.
  double scale{
      1.0};  // scaledNormal = normalize((<sampled normal texture value>
             // * 2.0 - 1.0) * vec3(<normal scale>, <normal scale>, 1.0))

  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  NormalTextureInfo() = default;
  DEFAULT_METHODS(NormalTextureInfo)
  bool operator==(const NormalTextureInfo &) const;
};

struct OcclusionTextureInfo {
  int index{-1};    // required
  int texCoord{0};  // The set index of texture's TEXCOORD attribute used for
                    // texture coordinate mapping.
  double strength{1.0};  // occludedColor = lerp(color, color * <sampled
                         // occlusion texture value>, <occlusion strength>)

  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  OcclusionTextureInfo() = default;
  DEFAULT_METHODS(OcclusionTextureInfo)
  bool operator==(const OcclusionTextureInfo &) const;
};

// pbrMetallicRoughness class defined in glTF 2.0 spec.
struct PbrMetallicRoughness {
  std::vector<double> baseColorFactor{1.0, 1.0, 1.0, 1.0};  // len = 4. default [1,1,1,1]
  TextureInfo baseColorTexture;
  double metallicFactor{1.0};   // default 1
  double roughnessFactor{1.0};  // default 1
  TextureInfo metallicRoughnessTexture;

  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  PbrMetallicRoughness() = default;
  DEFAULT_METHODS(PbrMetallicRoughness)

  bool operator==(const PbrMetallicRoughness &) const;
};

// Each extension should be stored in a ParameterMap.
// members not in the values could be included in the ParameterMap
// to keep a single material model
struct Material {
  std::string name;

  std::vector<double> emissiveFactor{0.0, 0.0, 0.0};  // length 3. default [0, 0, 0]
  std::string alphaMode{"OPAQUE"}; // default "OPAQUE"
  double alphaCutoff{0.5};        // default 0.5
  bool doubleSided{false};        // default false
  std::vector<int> lods;          // level of detail materials (MSFT_lod)

  PbrMetallicRoughness pbrMetallicRoughness;

  NormalTextureInfo normalTexture;
  OcclusionTextureInfo occlusionTexture;
  TextureInfo emissiveTexture;

  // For backward compatibility
  // TODO(syoyo): Remove `values` and `additionalValues` in the next release.
  ParameterMap values;
  ParameterMap additionalValues;

  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  Material() = default;
  DEFAULT_METHODS(Material)

  bool operator==(const Material &) const;
};

struct BufferView {
  std::string name;
  int buffer{-1};        // Required
  size_t byteOffset{0};  // minimum 0, default 0
  size_t byteLength{0};  // required, minimum 1. 0 = invalid
  size_t byteStride{0};  // minimum 4, maximum 252 (multiple of 4), default 0 =
                         // understood to be tightly packed
  int target{0};  // ["ARRAY_BUFFER", "ELEMENT_ARRAY_BUFFER"] for vertex indices
                  // or attribs. Could be 0 for other data
  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  bool dracoDecoded{false};  // Flag indicating this has been draco decoded

  BufferView() = default;
  DEFAULT_METHODS(BufferView)
  bool operator==(const BufferView &) const;
};

struct Accessor {
  int bufferView{-1};  // optional in spec but required here since sparse
                       // accessor are not supported
  std::string name;
  size_t byteOffset{0};
  bool normalized{false};  // optional.
  int componentType{-1};   // (required) One of TINYGLTF_COMPONENT_TYPE_***
  size_t count{0};         // required
  int type{-1};            // (required) One of TINYGLTF_TYPE_***   ..
  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  std::vector<double>
      minValues;  // optional. integer value is promoted to double
  std::vector<double>
      maxValues;  // optional. integer value is promoted to double

  struct Sparse {
    int count;
    bool isSparse;
    struct {
      size_t byteOffset;
      int bufferView;
      int componentType;  // a TINYGLTF_COMPONENT_TYPE_ value
      Value extras;
      ExtensionMap extensions;
      std::string extras_json_string;
      std::string extensions_json_string;
    } indices;
    struct {
      int bufferView;
      size_t byteOffset;
      Value extras;
      ExtensionMap extensions;
      std::string extras_json_string;
      std::string extensions_json_string;
    } values;
    Value extras;
    ExtensionMap extensions;
    std::string extras_json_string;
    std::string extensions_json_string;
  };

  Sparse sparse;

  ///
  /// Utility function to compute byteStride for a given bufferView object.
  /// Returns -1 upon invalid glTF value or parameter configuration.
  ///
  int ByteStride(const BufferView &bufferViewObject) const {
    if (bufferViewObject.byteStride == 0) {
      // Assume data is tightly packed.
      int componentSizeInBytes =
          GetComponentSizeInBytes(static_cast<uint32_t>(componentType));
      if (componentSizeInBytes <= 0) {
        return -1;
      }

      int numComponents = GetNumComponentsInType(static_cast<uint32_t>(type));
      if (numComponents <= 0) {
        return -1;
      }

      return componentSizeInBytes * numComponents;
    } else {
      // Check if byteStride is a multiple of the size of the accessor's
      // component type.
      int componentSizeInBytes =
          GetComponentSizeInBytes(static_cast<uint32_t>(componentType));
      if (componentSizeInBytes <= 0) {
        return -1;
      }

      if ((bufferViewObject.byteStride % uint32_t(componentSizeInBytes)) != 0) {
        return -1;
      }
      return static_cast<int>(bufferViewObject.byteStride);
    }

    // unreachable return 0;
  }

  Accessor()

  {
    sparse.isSparse = false;
  }
  DEFAULT_METHODS(Accessor)
  bool operator==(const tinygltf::Accessor &) const;
};

struct PerspectiveCamera {
  double aspectRatio{0.0};  // min > 0
  double yfov{0.0};         // required. min > 0
  double zfar{0.0};         // min > 0
  double znear{0.0};        // required. min > 0

  PerspectiveCamera() = default;
  DEFAULT_METHODS(PerspectiveCamera)
  bool operator==(const PerspectiveCamera &) const;

  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
};

struct OrthographicCamera {
  double xmag{0.0};   // required. must not be zero.
  double ymag{0.0};   // required. must not be zero.
  double zfar{0.0};   // required. `zfar` must be greater than `znear`.
  double znear{0.0};  // required

  OrthographicCamera() = default;
  DEFAULT_METHODS(OrthographicCamera)
  bool operator==(const OrthographicCamera &) const;

  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
};

struct Camera {
  std::string type;  // required. "perspective" or "orthographic"
  std::string name;

  PerspectiveCamera perspective;
  OrthographicCamera orthographic;

  Camera() = default;
  DEFAULT_METHODS(Camera)
  bool operator==(const Camera &) const;

  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
};

struct Primitive {
  std::map<std::string, int> attributes;  // (required) A dictionary object of
                                          // integer, where each integer
                                          // is the index of the accessor
                                          // containing an attribute.
  int material{-1};  // The index of the material to apply to this primitive
                     // when rendering.
  int indices{-1};   // The index of the accessor that contains the indices.
  int mode{-1};      // one of TINYGLTF_MODE_***
  std::vector<std::map<std::string, int> > targets;  // array of morph targets,
  // where each target is a dict with attributes in ["POSITION, "NORMAL",
  // "TANGENT"] pointing
  // to their corresponding accessors
  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  Primitive() = default;
  DEFAULT_METHODS(Primitive)
  bool operator==(const Primitive &) const;
};

struct Mesh {
  std::string name;
  std::vector<Primitive> primitives;
  std::vector<double> weights;  // weights to be applied to the Morph Targets
  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  Mesh() = default;
  DEFAULT_METHODS(Mesh)
  bool operator==(const Mesh &) const;
};

class Node {
 public:
  Node() = default;

  DEFAULT_METHODS(Node)

  bool operator==(const Node &) const;

  int camera{-1};  // the index of the camera referenced by this node

  std::string name;
  int skin{-1};
  int mesh{-1};
  int light{-1};    // light source index (KHR_lights_punctual)
  int emitter{-1};  // audio emitter index (KHR_audio)
  std::vector<int> lods; // level of detail nodes (MSFT_lod)
  std::vector<int> children;
  std::vector<double> rotation;     // length must be 0 or 4
  std::vector<double> scale;        // length must be 0 or 3
  std::vector<double> translation;  // length must be 0 or 3
  std::vector<double> matrix;       // length must be 0 or 16
  std::vector<double> weights;  // The weights of the instantiated Morph Target

  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
};

struct Buffer {
  std::string name;
  std::vector<unsigned char> data;
  std::string
      uri;  // considered as required here but not in the spec (need to clarify)
            // uri is not decoded(e.g. whitespace may be represented as %20)
  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  Buffer() = default;
  DEFAULT_METHODS(Buffer)
  bool operator==(const Buffer &) const;
};

struct Asset {
  std::string version = "2.0";  // required
  std::string generator;
  std::string minVersion;
  std::string copyright;
  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  Asset() = default;
  DEFAULT_METHODS(Asset)
  bool operator==(const Asset &) const;
};

struct Scene {
  std::string name;
  std::vector<int> nodes;
  std::vector<int> audioEmitters;  // KHR_audio global emitters

  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;

  Scene() = default;
  DEFAULT_METHODS(Scene)
  bool operator==(const Scene &) const;
};

struct SpotLight {
  double innerConeAngle{0.0};
  double outerConeAngle{0.7853981634};

  SpotLight() = default;
  DEFAULT_METHODS(SpotLight)
  bool operator==(const SpotLight &) const;

  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
};

struct Light {
  std::string name;
  std::vector<double> color;
  double intensity{1.0};
  std::string type;
  double range{0.0};  // 0.0 = infinite
  SpotLight spot;

  Light() = default;
  DEFAULT_METHODS(Light)

  bool operator==(const Light &) const;

  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
};

struct PositionalEmitter {
  double coneInnerAngle{6.283185307179586};
  double coneOuterAngle{6.283185307179586};
  double coneOuterGain{0.0};
  double maxDistance{100.0};
  double refDistance{1.0};
  double rolloffFactor{1.0};

  PositionalEmitter() = default;
  DEFAULT_METHODS(PositionalEmitter)
  bool operator==(const PositionalEmitter &) const;

  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
};

struct AudioEmitter {
  std::string name;
  double gain{1.0};
  bool loop{false};
  bool playing{false};
  std::string
      type;  // positional - Positional audio emitters. Using sound cones, the
             // orientation is +Z having the same front side for a glTF asset.
             // global - Global audio emitters are not affected by the position
             // of audio listeners. coneInnerAngle, coneOuterAngle,
             // coneOuterGain, distanceModel, maxDistance, refDistance, and
             // rolloffFactor should all be ignored when set.
  std::string
      distanceModel;  // linear - A linear distance model calculating the
                      // gain induced by the distance according to: 1.0
                      // - rolloffFactor * (distance - refDistance) /
                      // (maxDistance - refDistance)
                      // inverse - (default) An inverse distance model
                      // calculating the gain induced by the distance according
                      // to: refDistance / (refDistance + rolloffFactor *
                      // (Math.max(distance, refDistance) - refDistance))
                      // exponential - An exponential distance model calculating
                      // the gain induced by the distance according to:
                      // pow((Math.max(distance, refDistance) / refDistance,
                      // -rolloffFactor))
  PositionalEmitter positional;
  int source{-1};

  AudioEmitter() : type("global"), distanceModel("inverse") {}
  DEFAULT_METHODS(AudioEmitter)

  bool operator==(const AudioEmitter &) const;

  ExtensionMap extensions;
  Value extras;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
};

struct AudioSource {
  std::string name;
  std::string uri;
  int bufferView{-1};  // (required if no uri)
  std::string
      mimeType;  // (required if no uri) The audio's MIME type. Required if
                 // bufferView is defined. Unless specified by another
                 // extension, the only supported mimeType is audio/mpeg.

  AudioSource() = default;
  DEFAULT_METHODS(AudioSource)

  bool operator==(const AudioSource &) const;

  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
};

class Model {
 public:
  Model() = default;
  DEFAULT_METHODS(Model)

  bool operator==(const Model &) const;

  std::vector<Accessor> accessors;
  std::vector<Animation> animations;
  std::vector<Buffer> buffers;
  std::vector<BufferView> bufferViews;
  std::vector<Material> materials;
  std::vector<Mesh> meshes;
  std::vector<Node> nodes;
  std::vector<Texture> textures;
  std::vector<Image> images;
  std::vector<Skin> skins;
  std::vector<Sampler> samplers;
  std::vector<Camera> cameras;
  std::vector<Scene> scenes;
  std::vector<Light> lights;
  std::vector<AudioEmitter> audioEmitters;
  std::vector<AudioSource> audioSources;

  int defaultScene{-1};
  std::vector<std::string> extensionsUsed;
  std::vector<std::string> extensionsRequired;

  Asset asset;

  Value extras;
  ExtensionMap extensions;

  // Filled when SetStoreOriginalJSONForExtrasAndExtensions is enabled.
  std::string extras_json_string;
  std::string extensions_json_string;
};

enum SectionCheck {
  NO_REQUIRE = 0x00,
  REQUIRE_VERSION = 0x01,
  REQUIRE_SCENE = 0x02,
  REQUIRE_SCENES = 0x04,
  REQUIRE_NODES = 0x08,
  REQUIRE_ACCESSORS = 0x10,
  REQUIRE_BUFFERS = 0x20,
  REQUIRE_BUFFER_VIEWS = 0x40,
  REQUIRE_ALL = 0x7f
};

///
/// URIEncodeFunction type. Signature for custom URI encoding of external
/// resources such as .bin and image files. Used by tinygltf to re-encode the
/// final location of saved files. object_type may be used to encode buffer and
/// image URIs differently, for example. See
/// https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#uris
///
using URIEncodeFunction = std::function<bool(
    const std::string & /* in_uri */, const std::string & /* object_type */,
    std::string * /* out_uri */, void * /* user_data */)>;

///
/// URIDecodeFunction type. Signature for custom URI decoding of external
/// resources such as .bin and image files. Used by tinygltf when computing
/// filenames to write resources.
///
using URIDecodeFunction =
    std::function<bool(const std::string & /* in_uri */,
                       std::string * /* out_uri */, void * /* user_data */)>;

// Declaration of default uri decode function
bool URIDecode(const std::string &in_uri, std::string *out_uri,
               void *user_data);

///
/// A structure containing URI callbacks and a pointer to their user data.
///
struct URICallbacks {
  URIEncodeFunction encode;  // Optional encode method
  URIDecodeFunction decode;  // Required decode method

  void *user_data;  // An argument that is passed to all uri callbacks
};

///
/// FileExistsFunction type. Signature for custom filesystem callbacks.
///
using FileExistsFunction = std::function<bool(
    const std::string & /* abs_filename */, void * /* user_data */)>;

///
/// ExpandFilePathFunction type. Signature for custom filesystem callbacks.
///
using ExpandFilePathFunction =
    std::function<std::string(const std::string &, void *)>;

///
/// ReadWholeFileFunction type. Signature for custom filesystem callbacks.
///
using ReadWholeFileFunction = std::function<bool(
    std::vector<unsigned char> *, std::string *, const std::string &, void *)>;

///
/// WriteWholeFileFunction type. Signature for custom filesystem callbacks.
///
using WriteWholeFileFunction =
    std::function<bool(std::string *, const std::string &,
                       const std::vector<unsigned char> &, void *)>;

///
/// GetFileSizeFunction type. Signature for custom filesystem callbacks.
///
using GetFileSizeFunction =
    std::function<bool(size_t *filesize_out, std::string *err,
                       const std::string &abs_filename, void *userdata)>;

///
/// A structure containing all required filesystem callbacks and a pointer to
/// their user data.
///
struct FsCallbacks {
  FileExistsFunction FileExists;
  ExpandFilePathFunction ExpandFilePath;
  ReadWholeFileFunction ReadWholeFile;
  WriteWholeFileFunction WriteWholeFile;
  GetFileSizeFunction GetFileSizeInBytes;  // To avoid GetFileSize Win32 API,
                                           // add `InBytes` suffix.

  void *user_data;  // An argument that is passed to all fs callbacks
};

#ifndef TINYGLTF_NO_FS
// Declaration of default filesystem callbacks

bool FileExists(const std::string &abs_filename, void *);

///
/// Expand file path(e.g. `~` to home directory on posix, `%APPDATA%` to
/// `C:\\Users\\tinygltf\\AppData`)
///
/// @param[in] filepath File path string. Assume UTF-8
/// @param[in] userdata User data. Set to `nullptr` if you don't need it.
///
std::string ExpandFilePath(const std::string &filepath, void *userdata);

bool ReadWholeFile(std::vector<unsigned char> *out, std::string *err,
                   const std::string &filepath, void *);

bool WriteWholeFile(std::string *err, const std::string &filepath,
                    const std::vector<unsigned char> &contents, void *);

bool GetFileSizeInBytes(size_t *filesize_out, std::string *err,
                        const std::string &filepath, void *);
#endif

///
/// LoadImageDataFunction type. Signature for custom image loading callbacks.
///
using LoadImageDataFunction = std::function<bool(
    Image * /* image */, const int /* image_idx */, std::string * /* err */,
    std::string * /* warn */, int /* req_width */, int /* req_height */,
    const unsigned char * /* bytes */, int /* size */, void * /*user_data */)>;

///
/// WriteImageDataFunction type. Signature for custom image writing callbacks.
/// The out_uri parameter becomes the URI written to the gltf and may reference
/// a file or contain a data URI.
///
using WriteImageDataFunction = std::function<bool(
    const std::string * /* basepath */, const std::string * /* filename */,
    const Image *image, bool /* embedImages */,
    const FsCallbacks * /* fs_cb */, const URICallbacks * /* uri_cb */,
    std::string * /* out_uri */, void * /* user_pointer */)>;

#ifndef TINYGLTF_NO_STB_IMAGE
// Declaration of default image loader callback
bool LoadImageData(Image *image, const int image_idx, std::string *err,
                   std::string *warn, int req_width, int req_height,
                   const unsigned char *bytes, int size, void *);
#endif

#ifndef TINYGLTF_NO_STB_IMAGE_WRITE
// Declaration of default image writer callback
bool WriteImageData(const std::string *basepath, const std::string *filename,
                    const Image *image, bool embedImages,
                    const FsCallbacks* fs_cb, const URICallbacks *uri_cb,
                    std::string *out_uri, void *);
#endif

///
/// glTF Parser/Serializer context.
///
class TinyGLTF {
 public:
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++98-compat"
#endif

  TinyGLTF() = default;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

  ~TinyGLTF() = default;

  ///
  /// Loads glTF ASCII asset from a file.
  /// Set warning message to `warn` for example it fails to load asserts.
  /// Returns false and set error string to `err` if there's an error.
  ///
  bool LoadASCIIFromFile(Model *model, std::string *err, std::string *warn,
                         const std::string &filename,
                         unsigned int check_sections = REQUIRE_VERSION);

  ///
  /// Loads glTF ASCII asset from string(memory).
  /// `length` = strlen(str);
  /// `base_dir` is a search path of glTF asset(e.g. images). Path Must be an
  /// expanded path (e.g. no tilde(`~`), no environment variables). Set warning
  /// message to `warn` for example it fails to load asserts. Returns false and
  /// set error string to `err` if there's an error.
  ///
  bool LoadASCIIFromString(Model *model, std::string *err, std::string *warn,
                           const char *str, const unsigned int length,
                           const std::string &base_dir,
                           unsigned int check_sections = REQUIRE_VERSION);

  ///
  /// Loads glTF binary asset from a file.
  /// Set warning message to `warn` for example it fails to load asserts.
  /// Returns false and set error string to `err` if there's an error.
  ///
  bool LoadBinaryFromFile(Model *model, std::string *err, std::string *warn,
                          const std::string &filename,
                          unsigned int check_sections = REQUIRE_VERSION);

  ///
  /// Loads glTF binary asset from memory.
  /// `length` = strlen(str);
  /// `base_dir` is a search path of glTF asset(e.g. images). Path Must be an
  /// expanded path (e.g. no tilde(`~`), no environment variables).
  /// Set warning message to `warn` for example it fails to load asserts.
  /// Returns false and set error string to `err` if there's an error.
  ///
  bool LoadBinaryFromMemory(Model *model, std::string *err, std::string *warn,
                            const unsigned char *bytes,
                            const unsigned int length,
                            const std::string &base_dir = "",
                            unsigned int check_sections = REQUIRE_VERSION);

  ///
  /// Write glTF to stream, buffers and images will be embedded
  ///
  bool WriteGltfSceneToStream(const Model *model, std::ostream &stream,
                              bool prettyPrint, bool writeBinary);

  ///
  /// Write glTF to file.
  ///
  bool WriteGltfSceneToFile(const Model *model, const std::string &filename,
                            bool embedImages, bool embedBuffers,
                            bool prettyPrint, bool writeBinary);

  ///
  /// Sets the parsing strictness.
  ///
  void SetParseStrictness(ParseStrictness strictness);

  ///
  /// Set callback to use for loading image data. Passing the nullptr is akin to
  /// calling RemoveImageLoader().
  ///
  void SetImageLoader(LoadImageDataFunction LoadImageData, void *user_data);

  ///
  /// Unset(remove) callback of loading image data
  ///
  void RemoveImageLoader();

  ///
  /// Set callback to use for writing image data
  ///
  void SetImageWriter(WriteImageDataFunction WriteImageData, void *user_data);

  ///
  /// Set callbacks to use for URI encoding and decoding and their user data.
  /// Returns false if there is an error with the callbacks. If err is not
  /// nullptr, explanation will be written there.
  ///
  bool SetURICallbacks(URICallbacks callbacks, std::string* err = nullptr);

  ///
  /// Set callbacks to use for filesystem (fs) access and their user data.
  /// Returns false if there is an error with the callbacks. If err is not
  /// nullptr, explanation will be written there.
  ///
  bool SetFsCallbacks(FsCallbacks callbacks, std::string* err = nullptr);

  ///
  /// Set serializing default values(default = false).
  /// When true, default values are force serialized to .glTF.
  /// This may be helpful if you want to serialize a full description of glTF
  /// data.
  ///
  /// TODO(LTE): Supply parsing option as function arguments to
  /// `LoadASCIIFromFile()` and others, not by a class method
  ///
  void SetSerializeDefaultValues(const bool enabled) {
    serialize_default_values_ = enabled;
  }

  bool GetSerializeDefaultValues() const { return serialize_default_values_; }

  ///
  /// Store original JSON string for `extras` and `extensions`.
  /// This feature will be useful when the user want to reconstruct custom data
  /// structure from JSON string.
  ///
  void SetStoreOriginalJSONForExtrasAndExtensions(const bool enabled) {
    store_original_json_for_extras_and_extensions_ = enabled;
  }

  bool GetStoreOriginalJSONForExtrasAndExtensions() const {
    return store_original_json_for_extras_and_extensions_;
  }

  ///
  /// Specify whether preserve image channels when loading images or not.
  /// (Not effective when the user supplies their own LoadImageData callbacks)
  ///
  void SetPreserveImageChannels(bool onoff) {
    preserve_image_channels_ = onoff;
  }

  bool GetPreserveImageChannels() const { return preserve_image_channels_; }

  ///
  /// Specifiy whether image data is decoded/decompressed during load, or left as is
  ///
  void SetImagesAsIs(bool onoff) {
      images_as_is_ = onoff;
  }

  bool GetImagesAsIs() const { return images_as_is_; }

  ///
  /// Set maximum allowed external file size in bytes.
  /// Default: 2GB
  /// Only effective for built-in ReadWholeFileFunction FS function.
  ///
  void SetMaxExternalFileSize(size_t max_bytes) {
    max_external_file_size_ = max_bytes;
  }

  size_t GetMaxExternalFileSize() const { return max_external_file_size_; }

 private:
  ///
  /// Loads glTF asset from string(memory).
  /// `length` = strlen(str);
  /// Set warning message to `warn` for example it fails to load asserts
  /// Returns false and set error string to `err` if there's an error.
  ///
  bool LoadFromString(Model *model, std::string *err, std::string *warn,
                      const char *str, const unsigned int length,
                      const std::string &base_dir, unsigned int check_sections);

  const unsigned char *bin_data_ = nullptr;
  size_t bin_size_ = 0;
  bool is_binary_ = false;

  ParseStrictness strictness_ = ParseStrictness::Strict;

  bool serialize_default_values_ = false;  ///< Serialize default values?

  bool store_original_json_for_extras_and_extensions_ = false;

  bool preserve_image_channels_ = false;  /// Default false(expand channels to
                                          /// RGBA) for backward compatibility.

  bool images_as_is_ = false; /// Default false (decode/decompress images)

  size_t max_external_file_size_{
      size_t((std::numeric_limits<int32_t>::max)())};  // Default 2GB

  // Warning & error messages
  std::string warn_;
  std::string err_;

  FsCallbacks fs = {
#ifndef TINYGLTF_NO_FS
      &tinygltf::FileExists,
      &tinygltf::ExpandFilePath,
      &tinygltf::ReadWholeFile,
      &tinygltf::WriteWholeFile,
      &tinygltf::GetFileSizeInBytes,

      nullptr  // Fs callback user data
#else
      nullptr, nullptr, nullptr, nullptr, nullptr,

      nullptr  // Fs callback user data
#endif
  };

  URICallbacks uri_cb = {
      // Use paths as-is by default. This will use JSON string escaping.
      nullptr,
      // Decode all URIs before using them as paths as the application may have
      // percent encoded them.
      &tinygltf::URIDecode,
      // URI callback user data
      nullptr};

  LoadImageDataFunction LoadImageData =
#ifndef TINYGLTF_NO_STB_IMAGE
      &tinygltf::LoadImageData;
#else
      nullptr;
#endif
  void *load_image_user_data_{nullptr};
  bool user_image_loader_{false};

  WriteImageDataFunction WriteImageData =
#ifndef TINYGLTF_NO_STB_IMAGE_WRITE
      &tinygltf::WriteImageData;
#else
      nullptr;
#endif
  void *write_image_user_data_{nullptr};
};

#ifdef __clang__
#pragma clang diagnostic pop  // -Wpadded
#endif

}  // namespace tinygltf

#endif