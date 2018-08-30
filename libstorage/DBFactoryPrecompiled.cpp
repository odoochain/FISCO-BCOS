#include "DBFactoryPrecompiled.h"
#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <libdevcrypto/Common.h>
#include <libethcore/ABI.h>
#include <libdevcore/easylog.h>
#include <libdevcore/Hash.h>
#include "DBPrecompiled.h"
#include "MemoryStateDB.h"

using namespace dev;
using namespace dev::precompiled;

void DBFactoryPrecompiled::beforeBlock(std::shared_ptr<PrecompiledContext> context) {
}

void DBFactoryPrecompiled::afterBlock(std::shared_ptr<PrecompiledContext> context, bool commit) {
	if(!commit) {
		LOG(DEBUG) << "Clear _name2Table: " << _name2Table.size();
		_name2Table.clear();

		return;
	}

	LOG(DEBUG) << "Submiting DBPrecopiled";

	//汇总所有表的数据并提交
	std::vector<dev::storage::TableData::Ptr> datas;

	for (auto dbIt : _name2Table) {
		DBPrecompiled::Ptr dbPrecompiled = std::dynamic_pointer_cast<DBPrecompiled>(context->getPrecompiled(dbIt.second));

		dev::storage::TableData::Ptr tableData = std::make_shared<dev::storage::TableData>();
		tableData->tableName = dbIt.first;
		for(auto it: *(dbPrecompiled->getDB()->data())) {
			if(it.second->dirty() || !_memoryDBFactory->stateStorage()->onlyDirty()) {
				tableData->data.insert(std::make_pair(it.first, it.second));
			}
		}

		if(!tableData->data.empty()) {
			datas.push_back(tableData);
		}
	}

	LOG(DEBUG) << "Total: " << datas.size() << " key";
	if(!datas.empty()) {
		if(_hash == h256()) {
			hash(context);
		}

		LOG(DEBUG) << "Submit data:" << datas.size() << " hash:" << _hash;
		_memoryDBFactory->stateStorage()->commit(
			context->blockInfo().hash,
			context->blockInfo().number.convert_to<int>(),
			datas,
			context->blockInfo().hash);
	}

	_name2Table.clear();
}

std::string DBFactoryPrecompiled::toString(std::shared_ptr<PrecompiledContext>) {
	return "DBFactory";
}

bytes DBFactoryPrecompiled::call(std::shared_ptr<PrecompiledContext> context, bytesConstRef param) {
	LOG(TRACE) << "this: " << this << " call DBFactory:" << toHex(param);

	//解析出函数名
	uint32_t func = getParamFunc(param);
	bytesConstRef data = getParamData(param);

	LOG(DEBUG) << "func:" << std::hex << func;

	dev::eth::ContractABI abi;

	bytes out;

	switch (func) {
	case 0xc184e0ff: //openDB(string)
	case 0xf23f63c9: { //openTable(string)
		std::string str;
		abi.abiOut(data, str);

		LOG(DEBUG) << "DBFactory open table:" << str;

		auto it = _name2Table.find(str);
		if(it != _name2Table.end()) {
			LOG(DEBUG) << "Table:" << context->blockInfo().hash << " already opened:" << it->second;
			out = abi.abiIn("", it->second);

			break;
		}

		LOG(DEBUG) << "Open new table:" << str;

		dev::storage::StateDB::Ptr db = _memoryDBFactory->openTable(context->blockInfo().hash, context->blockInfo().number.convert_to<int>(), str);

		DBPrecompiled::Ptr dbPrecompiled = std::make_shared<DBPrecompiled>();
		dbPrecompiled->setDB(db);
		dbPrecompiled->setStringFactoryPrecompiled(_stringFactoryPrecompiled);

		Address address = context->registerPrecompiled(dbPrecompiled);
		_name2Table.insert(std::make_pair(str, address));

		out = abi.abiIn("", address);

		break;
	}
	case 0x56004b6a: { //createTable(string,string,string)
		std::string tableName;
		std::string keyName;
		std::string fieldNames;

		abi.abiOut(data, tableName, keyName, fieldNames);
		std::vector<std::string> fieldNameList;
		boost::split(fieldNameList, fieldNames, boost::is_any_of(","));

		auto table = _memoryDBFactory->createTable(context->blockInfo().hash, context->blockInfo().number.convert_to<int>(), tableName, keyName, fieldNameList);

		DBPrecompiled::Ptr dbPrecompiled = std::make_shared<DBPrecompiled>();
		dbPrecompiled->setDB(table);
		dbPrecompiled->setStringFactoryPrecompiled(_stringFactoryPrecompiled);

		Address address = context->registerPrecompiled(dbPrecompiled);
		_name2Table.insert(std::make_pair(tableName, address));

		out = abi.abiIn("", address);

		break;
	}
	default: {
		break;
	}
	}

	return out;
}

h256 DBFactoryPrecompiled::hash(std::shared_ptr<PrecompiledContext> context) {
	bytes data;

	//汇总所有表的hash，算一个hash
	LOG(DEBUG) << "this: " << this << " 总计表:" << _name2Table.size();
	for(auto dbAddress: _name2Table) {
		DBPrecompiled::Ptr db = std::dynamic_pointer_cast<DBPrecompiled>(context->getPrecompiled(dbAddress.second));
		h256 hash = db->hash();
		LOG(DEBUG) << "表:" << dbAddress.first << " hash:" << hash;
		if(hash == h256()) {
			continue;
		}

		bytes dbHash = db->hash().asBytes();

		data.insert(data.end(), dbHash.begin(), dbHash.end());
	}

	if(data.empty()) {
		return h256();
	}

	LOG(DEBUG) << "计算DBFactoryPrecompiled data:" << data << " hash:" << dev::sha256(&data);

	_hash = dev::sha256(&data);
	return _hash;
}
