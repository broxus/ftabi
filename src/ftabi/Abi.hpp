#pragma once

#include <block/block-parse.h>
#include <common/checksum.h>
#include <crypto/Ed25519.h>
#include <crypto/block/check-proof.h>
#include <crypto/block/mc-config.h>
#include <crypto/vm/vm.h>
#include <tdutils/td/utils/optional.h>

#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace ftabi
{
using BuilderData = td::Ref<vm::DataCell>;
using SliceData = td::Ref<vm::CellSlice>;

enum class ParamType { Uint, Int, Bool, Tuple, Array, FixedArray, Cell, Map, Address, Bytes, FixedBytes, Gram, Time, Expire, PublicKey };

struct Param;
using ParamRef = td::Ref<Param>;

struct Value;
using ValueRef = td::Ref<Value>;

struct Param : public td::CntObject {
    explicit Param(std::string name, ParamType param_type)
        : name_{std::move(name)}
        , param_type_{param_type}
    {
    }
    auto name() const -> const std::string& { return name_; }
    auto type() const -> ParamType { return param_type_; }

    virtual auto type_signature() const -> std::string = 0;
    virtual auto bit_len() const -> size_t { return 0; }
    virtual auto default_value() const -> td::Result<ValueRef> { return td::Status::Error("type doesn't have default value and must be explicitly defined"); }
    auto make_copy() const -> Param* override = 0;

protected:
    std::string name_;
    ParamType param_type_;
};

struct Value : public td::CntObject {
    explicit Value(ParamRef param)
        : param_{std::move(param)}
    {
    }
    auto param() const -> const ParamRef& { return param_; }
    auto check_type(const ParamRef& expected) const -> bool { return param_->type_signature() == expected->type_signature(); }

    virtual auto serialize() const -> td::Result<std::vector<BuilderData>> = 0;
    virtual auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> = 0;
    virtual auto to_string() const -> std::string { return "unknown"; }
    auto make_copy() const -> Value* override = 0;

protected:
    ParamRef param_;
};

struct ValueInt : Value {
    explicit ValueInt(ParamRef param, const td::BigInt256& value);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final { return new ValueInt{param_, value}; }

    td::BigInt256 value;

private:
    auto try_is_signed() const -> td::Result<bool>;
};

struct ParamUint : Param {
    using ValueType = ValueInt;

    explicit ParamUint(const std::string& name, size_t size)
        : Param{name, ParamType::Uint}
        , size{size}
    {
    }
    auto type_signature() const -> std::string final { return "uint" + std::to_string(size); }
    auto bit_len() const -> size_t final { return size; }
    auto default_value() const -> td::Result<ValueRef> final { return ValueInt{ParamRef{make_copy()}, td::make_bigint(0)}; }
    auto make_copy() const -> Param* final { return new ParamUint{name_, size}; }

    size_t size;
};

struct ParamInt : Param {
    using ValueType = ValueInt;

    explicit ParamInt(const std::string& name, size_t size)
        : Param{name, ParamType::Int}
        , size{size}
    {
    }
    auto type_signature() const -> std::string final { return "int" + std::to_string(size); }
    auto bit_len() const -> size_t final { return size; }
    auto default_value() const -> td::Result<ValueRef> final { return ValueInt{ParamRef{make_copy()}, td::make_bigint(0)}; }
    auto make_copy() const -> Param* final { return new ParamInt{name_, size}; }

    size_t size;
};

struct ValueBool : Value {
    explicit ValueBool(ParamRef param, bool value);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final;

    bool value;
};

struct ParamBool : Param {
    using ValueType = ValueBool;

    explicit ParamBool(const std::string& name)
        : Param{name, ParamType::Bool}
    {
    }
    auto type_signature() const -> std::string final { return "bool"; }
    auto default_value() const -> td::Result<ValueRef> final { return ValueBool{ParamRef{make_copy()}, false}; }
    auto make_copy() const -> Param* final { return new ParamBool{name_}; }
};

struct ValueTuple : Value {
    explicit ValueTuple(ParamRef param, std::vector<ValueRef> values);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final;

    std::vector<ValueRef> values;
};

struct ParamTuple : Param {
    using ValueType = ValueTuple;

    explicit ParamTuple(const std::string& name, std::vector<ParamRef> items)
        : Param{name, ParamType::Tuple}
        , items{std::move(items)}
    {
    }
    template <typename Arg, typename... Args>
    explicit ParamTuple(const std::string& name, Arg&& arg, Args&&... args)
        : Param{name, ParamType::Tuple}
        , items{ParamRef{std::forward(arg)}, ParamRef{std::forward(args)}...}
    {
        static_assert(std::is_base_of_v<Param, Arg> && (std::is_base_of_v<Param, Args> && ...));
    }
    auto type_signature() const -> std::string final
    {
        std::string result{};
        if (items.empty()) {
            result = "()";
        }
        else {
            for (const auto& item : items) {
                result += ',';
                result += item->type_signature();
            }
            result[0] = '(';
            result += ')';
        }
        return result;
    }
    auto default_value() const -> td::Result<ValueRef> final
    {
        std::vector<ValueRef> result;
        result.reserve(items.size());
        for (const auto& item : items) {
            TRY_RESULT(item_value, item->default_value())
            result.emplace_back(item_value);
        }
        return ValueTuple{ParamRef{make_copy()}, std::move(result)};
    }
    auto make_copy() const -> Param* final { return new ParamTuple{name_, items}; }

    std::vector<ParamRef> items;
};

struct ParamArray : Param {
    explicit ParamArray(const std::string& name, ParamRef param)
        : Param{name, ParamType::Array}
        , param{std::move(param)}
    {
    }
    template <typename T>
    explicit ParamArray(const std::string& name, T&& param)
        : Param{name, ParamType::Array}
        , param{std::forward(param)}
    {
    }
    auto type_signature() const -> std::string final { return param->type_signature() + "[]"; }
    auto make_copy() const -> Param* final { return new ParamArray{name_, param}; }

    ParamRef param;
};

struct ParamFixedArray : Param {
    explicit ParamFixedArray(const std::string& name, ParamRef param, size_t size)
        : Param{name, ParamType::FixedArray}
        , param{std::move(param)}
        , size{size}
    {
    }
    template <typename T>
    explicit ParamFixedArray(const std::string& name, T&& param, size_t size)
        : Param{name, ParamType::FixedArray}
        , param{std::forward(param)}
        , size{size}
    {
    }
    auto type_signature() const -> std::string final { return param->type_signature() + "[" + std::to_string(size) + "]"; }
    auto make_copy() const -> Param* final { return new ParamFixedArray{name_, param, size}; }

    ParamRef param;
    size_t size;
};

struct ValueCell : Value {
    explicit ValueCell(ParamRef param, td::Ref<vm::Cell> value);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final;

    td::Ref<vm::Cell> value;
};

struct ParamCell : Param {
    using ValueType = ValueCell;

    explicit ParamCell(const std::string& name)
        : Param{name, ParamType::Cell}
    {
    }
    auto type_signature() const -> std::string final { return "cell"; }
    auto default_value() const -> td::Result<ValueRef> final { return ValueCell{ParamRef{make_copy()}, td::Ref<vm::Cell>{}}; }
    auto make_copy() const -> Param* final { return new ParamCell{name_}; }
};

struct ValueMap : Value {
    explicit ValueMap(ParamRef param, std::vector<std::pair<ValueRef, ValueRef>> values);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final;

    std::vector<std::pair<ValueRef, ValueRef>> values;
};

struct ParamMap : Param {
    explicit ParamMap(const std::string& name, ParamRef key, ParamRef value)
        : Param{name, ParamType::Map}
        , key{std::move(key)}
        , value{std::move(value)}
    {
    }
    template <typename K, typename V>
    explicit ParamMap(const std::string& name, K&& key, V&& value)
        : Param{name, ParamType::Map}
        , key{std::forward(key)}
        , value{std::forward(value)}
    {
    }
    auto type_signature() const -> std::string final { return "map(" + key->type_signature() + "," + value->type_signature() + ")"; }
    auto make_copy() const -> Param* final { return new ParamMap{name_, key, value}; }

    ParamRef key;
    ParamRef value;
};

struct ValueAddress : Value {
    explicit ValueAddress(ParamRef param, const block::StdAddress& value);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final;

    block::StdAddress value;
};

struct ParamAddress : Param {
    using ValueType = ValueAddress;

    explicit ParamAddress(const std::string& name)
        : Param{name, ParamType::Address}
    {
    }
    auto type_signature() const -> std::string final { return "address"; }
    auto default_value() const -> td::Result<ValueRef> final { return ValueAddress{ParamRef{make_copy()}, block::StdAddress{}}; }
    auto make_copy() const -> Param* final { return new ParamAddress{name_}; }
};

struct ValueBytes : Value {
    explicit ValueBytes(ParamRef param, std::vector<uint8_t> value);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final;

    std::vector<uint8_t> value;
};

struct ParamBytes : Param {
    using ValueType = ValueBytes;

    explicit ParamBytes(const std::string& name)
        : Param{name, ParamType::Bytes}
    {
    }
    auto type_signature() const -> std::string final { return "bytes"; }
    auto default_value() const -> td::Result<ValueRef> final { return ValueBytes{ParamRef{make_copy()}, {}}; }
    auto make_copy() const -> Param* final { return new ParamBytes{name_}; }
};

struct ParamFixedBytes : Param {
    using ValueType = ValueBytes;

    explicit ParamFixedBytes(const std::string& name, size_t size)
        : Param{name, ParamType::FixedBytes}
        , size{size}
    {
    }
    auto type_signature() const -> std::string final { return "fixedbytes" + std::to_string(size); }
    auto default_value() const -> td::Result<ValueRef> final { return ValueBytes{ParamRef{make_copy()}, std::vector<uint8_t>(size, 0u)}; }
    auto make_copy() const -> Param* final { return new ParamFixedBytes{name_, size}; }

    size_t size;
};

struct ValueGram : Value {
    explicit ValueGram(ParamRef param, td::RefInt256 value);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final;

    td::RefInt256 value;
};

struct ParamGram : Param {
    using ValueType = ValueGram;

    explicit ParamGram(const std::string& name)
        : Param{name, ParamType::Gram}
    {
    }
    auto type_signature() const -> std::string final { return "gram"; }
    auto default_value() const -> td::Result<ValueRef> final { return ValueGram{ParamRef{make_copy()}, td::make_refint(0)}; }
    auto make_copy() const -> Param* final { return new ParamGram{name_}; }
};

struct ValueTime : Value {
    explicit ValueTime(ParamRef param, td::uint64 value);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final;

    td::uint64 value;
};

struct ParamTime : Param {
    using ValueType = ValueTime;

    explicit ParamTime(const std::string& name)
        : Param{name, ParamType::Time}
    {
    }
    auto type_signature() const -> std::string final { return "time"; }
    auto default_value() const -> td::Result<ValueRef> final
    {
        const auto duration = std::chrono::system_clock::now().time_since_epoch();
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
        return ValueTime{ParamRef{make_copy()}, static_cast<uint64_t>(milliseconds)};
    }
    auto make_copy() const -> Param* final { return new ParamTime{name_}; }
};

struct ValueExpire : Value {
    explicit ValueExpire(ParamRef param, uint32_t value);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final;

    uint32_t value;
};

struct ParamExpire : Param {
    using ValueType = ValueExpire;

    explicit ParamExpire(const std::string& name)
        : Param{name, ParamType::Expire}
    {
    }
    auto type_signature() const -> std::string final { return "expire"; }
    auto default_value() const -> td::Result<ValueRef> final { return ValueExpire{ParamRef{make_copy()}, std::numeric_limits<uint32_t>::max()}; }
    auto make_copy() const -> Param* final { return new ParamExpire{name_}; }
};

struct ValuePublicKey : Value {
    explicit ValuePublicKey(ParamRef param, std::optional<td::SecureString> value);
    auto serialize() const -> td::Result<std::vector<BuilderData>> final;
    auto deserialize(SliceData&& cursor, bool last) -> td::Result<SliceData> final;
    auto to_string() const -> std::string final;
    auto make_copy() const -> Value* final;

    std::optional<td::SecureString> value;
};

struct ParamPublicKey : Param {
    using ValueType = ValuePublicKey;

    explicit ParamPublicKey(const std::string& name)
        : Param{name, ParamType::PublicKey}
    {
    }
    auto type_signature() const -> std::string final { return "pubkey"; }
    auto default_value() const -> td::Result<ValueRef> final { return ValuePublicKey{ParamRef{make_copy()}, decltype(std::declval<ValuePublicKey>().value){}}; }
    auto make_copy() const -> Param* final { return new ParamPublicKey{name_}; }
};

auto fill_signature(const std::optional<td::SecureString>& signature, BuilderData&& cell) -> td::Result<BuilderData>;
auto pack_cells_into_chain(std::vector<BuilderData>&& cells) -> td::Result<BuilderData>;

using HeaderParams = std::vector<ParamRef>;
using InputParams = std::vector<ParamRef>;
using OutputParams = std::vector<ParamRef>;

using HeaderValues = std::unordered_map<std::string, ValueRef>;
using InputValues = std::vector<ValueRef>;

template <typename P, typename... Args>
static auto make_value(P&& param, Args&&... args) -> ValueRef
{
    using V = typename P::ValueType;
    static_assert(std::is_base_of_v<Param, P> && std::is_base_of_v<Value, V>);
    return ValueRef{V{ParamRef{param}, std::move(args)...}};
}

template <typename... Values>
static auto make_header(Values&&... values) -> HeaderValues
{
    static_assert((std::is_same_v<Values, ValueRef> && ...));

    HeaderValues header{};
    (header.insert(std::make_pair(values->param()->name(), values)), ...);
    return header;
}

template <typename... Args>
static auto make_params(Args&&... args) -> std::vector<ParamRef>
{
    static_assert((std::is_base_of_v<Param, Args> && ...));
    return std::vector{std::move(ParamRef{args})...};
}

auto check_params(const std::vector<ValueRef>& values, const std::vector<ParamRef>& params) -> bool;

static constexpr uint8_t ABI_VERSION = 2;

auto compute_function_id(const std::string& signature) -> uint32_t;
auto compute_function_signature(const std::string& name, const InputParams& inputs, const OutputParams& outputs) -> std::string;

struct FunctionCall : public td::CntObject {
    explicit FunctionCall(InputValues&& inputs);
    explicit FunctionCall(HeaderValues&& header, InputValues&& inputs);
    explicit FunctionCall(HeaderValues&& header, InputValues&& inputs, bool internal, std::optional<td::Ed25519::PrivateKey>&& private_key);

    auto make_copy() const -> FunctionCall* final;

    HeaderValues header{};
    InputValues inputs{};
    bool internal{};
    std::optional<td::Ed25519::PrivateKey> private_key{};
    bool body_as_ref{};
};

class Function : public td::CntObject {
public:
    explicit Function(std::string&& name, HeaderParams&& header, InputParams&& inputs, OutputParams&& outputs, uint32_t input_id, uint32_t output_id);
    explicit Function(std::string&& name, HeaderParams&& header, InputParams&& inputs, OutputParams&& outputs);
    explicit Function(std::string&& name, HeaderParams&& header, InputParams&& inputs, OutputParams&& outputs, uint32_t id);

    auto encode_input(FunctionCall& call) const -> td::Result<BuilderData>;
    auto encode_input(const td::Ref<FunctionCall>& call) const -> td::Result<BuilderData>;
    auto encode_input(const HeaderValues& header, const InputValues& inputs, bool internal, const std::optional<td::Ed25519::PrivateKey>& private_key) const
    -> td::Result<BuilderData>;

    auto decode_output(SliceData&& data) const -> td::Result<std::vector<ValueRef>>;
    auto decode_params(SliceData&& cursor) const -> td::Result<std::vector<ValueRef>>;

    auto encode_header(const HeaderValues& header, bool internal) const -> td::Result<std::vector<BuilderData>>;

    auto create_unsigned_call(const HeaderValues& header, const InputValues& inputs, bool internal, bool reserve_sign) const
    -> td::Result<std::pair<BuilderData, vm::CellHash>>;

    auto make_copy() const -> Function* final;

    auto has_input() const -> bool { return !inputs_.empty(); }
    auto has_output() const -> bool { return !outputs_.empty(); }

    auto input_id() const -> uint32_t { return input_id_; }
    auto output_id() const -> uint32_t { return output_id_; }

private:
    std::string name_{};
    HeaderParams header_{};
    InputParams inputs_{};
    OutputParams outputs_{};
    uint32_t input_id_ = 0;
    uint32_t output_id_ = 0;
};

enum class AccountState {
    empty,
    uninit,
    frozen,
    active,
    unknown,
};

static auto to_string(AccountState account_state) -> const char*
{
    switch (account_state) {
        case AccountState::empty:
            return "unknown";
        case AccountState::uninit:
            return "account_uninit";
        case AccountState::frozen:
            return "account_frozen";
        case AccountState::active:
            return "account_active";
        default:
            return "unknown";
    }
}

struct AccountStateInfo {
    ton::WorkchainId workchain;
    ton::StdSmcAddress addr;
    ton::UnixTime sync_utime;
    td::int64 balance;
    AccountState state;
    ton::LogicalTime last_transaction_lt;
    ton::Bits256 last_transaction_hash;

    block::AccountState state_details;
    block::AccountState::Info state_details_info;
};

auto run_smc_method(AccountStateInfo&& account, td::Ref<Function>&& function, td::Ref<FunctionCall>&& function_call) -> td::Result<std::vector<ValueRef>>;

}  // namespace ftabi
