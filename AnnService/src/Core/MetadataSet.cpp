// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License.

#include "inc/Core/MetadataSet.h"

#include <string.h>
#include <fstream>
#include <iostream>
#include <shared_mutex>

using namespace SPTAG;

ErrorCode
MetadataSet::RefineMetadata(std::vector<SizeType>& indices, std::shared_ptr<MetadataSet>& p_newMetadata) const
{
    p_newMetadata.reset(new MemMetadataSet());
    for (SizeType& t : indices) {
        p_newMetadata->Add(GetMetadata(t));
    }
    return ErrorCode::Success;
}

ErrorCode
MetadataSet::RefineMetadata(std::vector<SizeType>& indices, std::ostream& p_metaOut, std::ostream& p_metaIndexOut) const
{
    SizeType R = (SizeType)indices.size();
    p_metaIndexOut.write((char*)&R, sizeof(SizeType));
    std::uint64_t offset = 0;
    for (SizeType i = 0; i < R; i++) {
        p_metaIndexOut.write((char*)&offset, sizeof(std::uint64_t));
        ByteArray meta = GetMetadata(indices[i]);
        offset += meta.Length();
    }
    p_metaIndexOut.write((char*)&offset, sizeof(std::uint64_t));

    for (SizeType i = 0; i < R; i++) {
        ByteArray meta = GetMetadata(indices[i]);
        p_metaOut.write((char*)meta.Data(), sizeof(uint8_t)*meta.Length());
    }
    std::cout << "Save MetaIndex(" << R << ") Meta(" << offset << ")" << std::endl;
    return ErrorCode::Success;
}


ErrorCode 
MetadataSet::RefineMetadata(std::vector<SizeType>& indices, const std::string& p_metaFile, const std::string& p_metaindexFile) const
{
    std::ofstream metaOut(p_metaFile + "_tmp", std::ios::binary);
    std::ofstream metaIndexOut(p_metaindexFile, std::ios::binary);
    if (!metaOut.is_open() || !metaIndexOut.is_open()) return ErrorCode::FailedCreateFile;

    RefineMetadata(indices, metaOut, metaIndexOut);
    metaOut.close();
    metaIndexOut.close();

    if (fileexists(p_metaFile.c_str())) std::remove(p_metaFile.c_str());
    std::rename((p_metaFile + "_tmp").c_str(), p_metaFile.c_str());
    return ErrorCode::Success;
}


void
MetadataSet::AddBatch(MetadataSet& data)
{
    for (SizeType i = 0; i < data.Count(); i++)
    {
        Add(data.GetMetadata(i));
    }
}


MetadataSet::MetadataSet()
{
}


MetadataSet:: ~MetadataSet()
{
}


FileMetadataSet::FileMetadataSet(const std::string& p_metafile, const std::string& p_metaindexfile)
    : m_metaFile(p_metafile),
      m_metaindexFile(p_metaindexfile)
{
    m_fp = new std::ifstream(p_metafile, std::ifstream::binary);
    std::ifstream fpidx(p_metaindexfile, std::ifstream::binary);
    if (!m_fp->is_open() || !fpidx.is_open())
    {
        std::cerr << "ERROR: Cannot open meta files " << p_metafile << " and " << p_metaindexfile << "!" << std::endl;
        return;
    }

    fpidx.read((char *)&m_count, sizeof(m_count));
    m_pOffsets.resize(m_count + 1);
    fpidx.read((char *)m_pOffsets.data(), sizeof(std::uint64_t) * (m_count + 1));
    fpidx.close();
    std::cout << "Load MetaIndex(" << m_pOffsets.size() - 1 << ") Meta(" << m_pOffsets.back() << ")" << std::endl;
}


FileMetadataSet::~FileMetadataSet()
{
    if (m_fp)
    {
        m_fp->close();
        delete m_fp;
    }
}


ByteArray
FileMetadataSet::GetMetadata(SizeType p_vectorID) const
{
    std::uint64_t startoff = m_pOffsets[p_vectorID];
    std::uint64_t bytes = m_pOffsets[p_vectorID + 1] - startoff;
    if (p_vectorID < m_count) {
        m_fp->seekg(startoff, std::ios_base::beg);
        ByteArray b = ByteArray::Alloc(bytes);
        m_fp->read((char*)b.Data(), bytes);
        return b;
    }
    else {
        startoff -= m_pOffsets[m_count];
        return ByteArray((std::uint8_t*)m_newdata.data() + startoff, bytes, false);
    }
}


ByteArray
FileMetadataSet::GetMetadataCopy(SizeType p_vectorID) const
{
    std::uint64_t startoff = m_pOffsets[p_vectorID];
    std::uint64_t bytes = m_pOffsets[p_vectorID + 1] - startoff;
    ByteArray b = ByteArray::Alloc(bytes);
    if (p_vectorID < m_count) {
        m_fp->seekg(startoff, std::ios_base::beg);   
        m_fp->read((char*)b.Data(), bytes);
    }
    else {
        memcpy(b.Data(), m_newdata.data() + (startoff - m_pOffsets[m_count]), bytes);
    }
    return b;
}


SizeType
FileMetadataSet::Count() const
{
    return static_cast<SizeType>(m_pOffsets.size() - 1);
}


bool
FileMetadataSet::Available() const
{
    return m_fp && m_fp->is_open() && m_pOffsets.size() > 1;
}


std::pair<std::uint64_t, std::uint64_t> 
FileMetadataSet::BufferSize() const
{
    return std::make_pair(m_pOffsets[m_pOffsets.size() - 1], 
        sizeof(SizeType) + sizeof(std::uint64_t) * m_pOffsets.size());
}


void
FileMetadataSet::Add(const ByteArray& data)
{
    m_newdata.insert(m_newdata.end(), data.Data(), data.Data() + data.Length());
    m_pOffsets.push_back(m_pOffsets.back() + data.Length());
}


ErrorCode
FileMetadataSet::SaveMetadata(std::ostream& p_metaOut, std::ostream& p_metaIndexOut)
{
    SizeType count = Count();
    p_metaIndexOut.write((char*)&count, sizeof(SizeType));
    p_metaIndexOut.write((char*)m_pOffsets.data(), sizeof(std::uint64_t) * m_pOffsets.size());

    m_fp->seekg(0, std::ios_base::beg);

    int bufsize = 1000000;
    char* buf = new char[bufsize];
    while (!m_fp->eof()) {
        m_fp->read(buf, bufsize);
        p_metaOut.write(buf, m_fp->gcount());
    }
    delete[] buf;
    
    if (m_newdata.size() > 0) {
        p_metaOut.write((char*)m_newdata.data(), m_newdata.size());
    }
    std::cout << "Save MetaIndex(" << m_pOffsets.size() - 1 << ") Meta(" << m_pOffsets.back() << ")" << std::endl;
    return ErrorCode::Success;
}


ErrorCode
FileMetadataSet::SaveMetadata(const std::string& p_metaFile, const std::string& p_metaindexFile)
{
    std::ofstream metaOut(p_metaFile + "_tmp", std::ios::binary);
    std::ofstream metaIndexOut(p_metaindexFile, std::ios::binary);
    if (!metaOut.is_open() || !metaIndexOut.is_open()) return ErrorCode::FailedCreateFile;

    SaveMetadata(metaOut, metaIndexOut);
    metaOut.close();
    metaIndexOut.close();

    m_fp->close();
    if (fileexists(p_metaFile.c_str())) std::remove(p_metaFile.c_str());
    std::rename((p_metaFile + "_tmp").c_str(), p_metaFile.c_str());
    m_fp->open(p_metaFile, std::ifstream::binary);
    m_count = Count();
    m_newdata.clear();
    return ErrorCode::Success;
}


MemMetadataSet::MemMetadataSet(): m_count(0), m_metadataHolder(ByteArray::c_empty)
{
    m_offsets.push_back(0);
    m_lock.reset(new std::shared_timed_mutex, std::default_delete<std::shared_timed_mutex>());
}


void
MemMetadataSet::Init(std::istream& p_metain, std::istream& p_metaindexin)
{
    p_metaindexin.read((char *)&m_count, sizeof(m_count));
    m_offsets.resize(m_count + 1);
    p_metaindexin.read((char *)m_offsets.data(), sizeof(std::uint64_t) * (m_count + 1));

    m_metadataHolder = ByteArray::Alloc(m_offsets[m_count]);
    p_metain.read((char *)m_metadataHolder.Data(), m_metadataHolder.Length());
    m_lock.reset(new std::shared_timed_mutex, std::default_delete<std::shared_timed_mutex>());
    std::cout << "Load MetaIndex(" << m_offsets.size() - 1 << ") Meta(" << m_offsets.back() << ")" << std::endl;
}


MemMetadataSet::MemMetadataSet(std::istream& p_metain, std::istream& p_metaindexin)
{
    Init(p_metain, p_metaindexin);
}

MemMetadataSet::MemMetadataSet(const std::string& p_metafile, const std::string& p_metaindexfile)
{
    std::ifstream meta(p_metafile, std::ifstream::binary);
    std::ifstream metaidx(p_metaindexfile, std::ifstream::binary);
    if (!meta.is_open() || !metaidx.is_open())
    {
        std::cerr << "ERROR: Cannot open meta files " << p_metafile << " and " << p_metaindexfile << "!" << std::endl;
        return;
    }
    Init(meta, metaidx);
    meta.close();
    metaidx.close();
}


MemMetadataSet::MemMetadataSet(ByteArray p_metadata, ByteArray p_offsets, SizeType p_count)
    : m_metadataHolder(std::move(p_metadata)),
      m_count(p_count)
{
    const std::uint64_t* newdata = reinterpret_cast<const std::uint64_t*>(p_offsets.Data());
    m_offsets.assign(newdata, newdata + p_count + 1);
    m_lock.reset(new std::shared_timed_mutex, std::default_delete<std::shared_timed_mutex>());
}


MemMetadataSet::~MemMetadataSet()
{
}


ByteArray
MemMetadataSet::GetMetadata(SizeType p_vectorID) const
{
    std::shared_lock<std::shared_timed_mutex> lock(*static_cast<std::shared_timed_mutex*>(m_lock.get()));
    if (p_vectorID < m_count)
    {
        return ByteArray(m_metadataHolder.Data() + m_offsets[p_vectorID],
                         m_offsets[p_vectorID + 1] - m_offsets[p_vectorID],
                         false);
    }
    else if (p_vectorID < (SizeType)(m_offsets.size() - 1)) {
        return ByteArray((std::uint8_t*)m_newdata.data() + m_offsets[p_vectorID] - m_offsets[m_count],
            m_offsets[p_vectorID + 1] - m_offsets[p_vectorID],
            false);
    }

    return ByteArray::c_empty;
}


ByteArray
MemMetadataSet::GetMetadataCopy(SizeType p_vectorID) const
{
    std::shared_lock<std::shared_timed_mutex> lock(*static_cast<std::shared_timed_mutex*>(m_lock.get()));
    std::uint64_t startoff = m_offsets[p_vectorID];
    std::uint64_t bytes = m_offsets[p_vectorID + 1] - startoff;
    ByteArray b = ByteArray::Alloc(bytes);
    if (p_vectorID < m_count) {
        memcpy(b.Data(), m_metadataHolder.Data() + startoff, bytes);
    }
    else {
        memcpy(b.Data(), m_newdata.data() + (startoff - m_offsets[m_count]), bytes);
    }
    return b;
}


SizeType
MemMetadataSet::Count() const
{
    std::shared_lock<std::shared_timed_mutex> lock(*static_cast<std::shared_timed_mutex*>(m_lock.get()));
    return static_cast<SizeType>(m_offsets.size() - 1);
}


bool
MemMetadataSet::Available() const
{
    std::shared_lock<std::shared_timed_mutex> lock(*static_cast<std::shared_timed_mutex*>(m_lock.get()));
    return m_offsets.size() > 1;
}


std::pair<std::uint64_t, std::uint64_t>
MemMetadataSet::BufferSize() const
{
    std::shared_lock<std::shared_timed_mutex> lock(*static_cast<std::shared_timed_mutex*>(m_lock.get()));
    return std::make_pair(m_offsets.back(),
        sizeof(SizeType) + sizeof(std::uint64_t) * m_offsets.size());
}


void
MemMetadataSet::Add(const ByteArray& data)
{
    std::unique_lock<std::shared_timed_mutex> lock(*static_cast<std::shared_timed_mutex*>(m_lock.get()));
    m_newdata.insert(m_newdata.end(), data.Data(), data.Data() + data.Length());
    m_offsets.push_back(m_offsets.back() + data.Length());
}


ErrorCode
MemMetadataSet::SaveMetadata(std::ostream& p_metaOut, std::ostream& p_metaIndexOut)
{
    std::shared_lock<std::shared_timed_mutex> lock(*static_cast<std::shared_timed_mutex*>(m_lock.get()));
    SizeType count = Count();
    p_metaIndexOut.write((char*)&count, sizeof(SizeType));
    p_metaIndexOut.write((char*)m_offsets.data(), sizeof(std::uint64_t) * m_offsets.size());

    p_metaOut.write(reinterpret_cast<const char*>(m_metadataHolder.Data()), m_metadataHolder.Length());
    if (m_newdata.size() > 0) {
        p_metaOut.write((char*)m_newdata.data(), m_newdata.size());
    }
    std::cout << "Save MetaIndex(" << m_offsets.size() - 1 << ") Meta(" << m_offsets.back() << ")" << std::endl;
    return ErrorCode::Success;
}



ErrorCode
MemMetadataSet::SaveMetadata(const std::string& p_metaFile, const std::string& p_metaindexFile)
{
    std::ofstream metaOut(p_metaFile + "_tmp", std::ios::binary);
    std::ofstream metaIndexOut(p_metaindexFile, std::ios::binary);
    if (!metaOut.is_open() || !metaIndexOut.is_open()) return ErrorCode::FailedCreateFile;

    SaveMetadata(metaOut, metaIndexOut);
    metaOut.close();
    metaIndexOut.close();

    if (fileexists(p_metaFile.c_str())) std::remove(p_metaFile.c_str());
    std::rename((p_metaFile + "_tmp").c_str(), p_metaFile.c_str());
    return ErrorCode::Success;
}

