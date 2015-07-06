//
// <copyright file="EvalReader.h" company="Microsoft">
//     Copyright (c) Microsoft Corporation.  All rights reserved.
// </copyright>
//
#pragma once

#define DATAREADER_LOCAL
#include "DataReader.h"

namespace Microsoft { namespace MSR { namespace CNTK {

// Evaluation Reader class
// interface to pass to evaluation DLL
template<class ElemType>
class EvalReader : public IDataReader<ElemType>
{
    typedef typename IDataReader<ElemType>::LabelType LabelType;
    typedef typename IDataReader<ElemType>::LabelIdType LabelIdType;
private:
    std::map<std::wstring, std::vector<ElemType>*>* m_inputs; // our input data
    std::map<std::wstring, size_t>* m_dimensions; // the number of rows for the input data
    size_t m_recordCount; // count of records in this data
    size_t m_currentRecord; // next record number to read
    size_t m_mbSize;
    vector<size_t> m_switchFrame;
    size_t m_oldSig;
public:
    // Method to setup the data for the reader
    void SetData(std::map<std::wstring, std::vector<ElemType>*>* inputs, std::map<std::wstring, size_t>* dimensions)
    {
        m_inputs = inputs;
        m_dimensions = dimensions;
        m_currentRecord = 0;
        m_recordCount = 0;
        for (auto iter = inputs->begin(); iter != inputs->end(); ++iter)
        {
            // figure out the dimension of the data
            const std::wstring& val = iter->first;
            size_t count = (*inputs)[val]->size();
            size_t rows = (*dimensions)[val];
            size_t recordCount = count/rows;


            if (m_recordCount != 0)
            {
                // record count must be the same for all the data
                if (recordCount != m_recordCount)
                    RuntimeError("Record Count of %ls (%lux%lu) does not match the record count of previous entries (%lu).", val.c_str(), rows, recordCount, m_recordCount);
            }
            else
            {
                m_recordCount = recordCount;
            }
        }
    }

    void SetBoundary (size_t newSig)
    {
        if (m_switchFrame.size()==0)
        {
            m_oldSig = newSig;
            m_switchFrame.assign(1,0);
        } else
        {
            if (m_oldSig==newSig)
            {
                m_switchFrame[0] = m_mbSize+8888;
            }
            else
            {
                m_switchFrame[0] = 0;
                m_oldSig = newSig;
            }
        }

    }

    virtual void Init(const ConfigParameters& /*config*/)
    {
    }

    // Destroy - cleanup and remove this class
    // NOTE: this destroys the object, and it can't be used past this point
    virtual void Destroy()
    {
        delete this;
    }

    // EvalReader Constructor
    // config - [in] configuration parameters for the datareader 
    EvalReader(const ConfigParameters& config)
    {
        m_recordCount = m_currentRecord = 0;
        Init(config);
    }

    // Destructor - free up the matrix values we allocated
    virtual ~EvalReader()
    {
    }

    //StartMinibatchLoop - Startup a minibatch loop 
    // mbSize - [in] size of the minibatch (number of frames, etc.)
    // epoch - [in] epoch number for this loop
    // requestedEpochSamples - [in] number of samples to randomize, defaults to requestDataSize which uses the number of samples there are in the dataset
    virtual void StartMinibatchLoop(size_t mbSize, size_t /*epoch*/, size_t /*requestedEpochSamples=requestDataSize*/)
    {
        m_mbSize = min(mbSize,m_recordCount);
    }

    // GetMinibatch - Get the next minibatch (features and labels)
    // matrices - [in] a map with named matrix types (i.e. 'features', 'labels') mapped to the corresponing matrix, 
    //             [out] each matrix resized if necessary containing data. 
    // returns - true if there are more minibatches, false if no more minibatchs remain
    virtual bool GetMinibatch(std::map<std::wstring, Matrix<ElemType>*>& matrices)
    {
        // how many records are we reading this time
        size_t recordCount = min(m_mbSize, m_recordCount-m_currentRecord);

        // check to see if we are out of records in this current dataset
        if (m_currentRecord >= m_recordCount)
            return false;

        // loop through all the input vectors to copy the data over
        for (auto iter = m_inputs->begin(); iter != m_inputs->end(); ++iter)
        {
            // figure out the dimension of the data
            std::wstring val = iter->first;
            size_t rows = (*m_dimensions)[val];
            //size_t count = rows*recordCount;

            // find the output matrix we want to fill
            auto iterIn = matrices.find(val);

            // allocate the matrix if we don't have one yet
            if (iterIn == matrices.end())
            {
                RuntimeError("No matrix data found for key '%ls', cannot continue", val.c_str());
            }

            Matrix<ElemType>* matrix = iterIn->second;

            // resize to the proper size to hold the data
            matrix->Resize(rows, recordCount);

            // copy over the data
            std::vector<ElemType>* data = iter->second;
            //size_t  = m_currentRecord*rows;
            void* mat = &(*matrix)(0,0);
            size_t matSize = matrix->GetNumElements()*sizeof(ElemType);
            void* dataPtr = (void*)((ElemType*)data->data() + m_currentRecord*rows);
            size_t dataSize = rows*recordCount*sizeof(ElemType);
            memcpy_s(mat, matSize, dataPtr, dataSize);
        }

        // increment our record pointer
        m_currentRecord += recordCount;

        // return true if we returned any data whatsoever
        return true;
    }

    size_t NumberSlicesInEachRecurrentIter() {return 1;}

    void SetNbrSlicesEachRecurrentIter(const size_t ) {}
    void SetSentenceSegBatch(std::vector<size_t> &sentenceEnd)
    {
        sentenceEnd.resize(m_switchFrame.size());
        for (size_t i = 0; i < m_switchFrame.size(); i++)
        {
            sentenceEnd[i] = m_switchFrame[i];
        }
    }
    void SetSentenceSegBatch(Matrix<ElemType>&)
    {
        NOT_IMPLEMENTED;
    }
    void GetSentenceBoundary(std::vector<size_t> boundaryInfo)
    {
        m_switchFrame.resize(boundaryInfo.size());
        for (size_t i = 0; i < m_switchFrame.size(); i ++)
        {
            m_switchFrame[i] = boundaryInfo[i];
        }
    }

    void SetRandomSeed(int) { NOT_IMPLEMENTED;  }

    // GetLabelMapping - Gets the label mapping from integer index to label type 
    // returns - a map from numeric datatype to native label type 
    virtual const std::map<typename EvalReader<ElemType>::LabelIdType, typename EvalReader<ElemType>::LabelType>& GetLabelMapping(const std::wstring& /*sectionName*/) 
    {
        static std::map<typename EvalReader<ElemType>::LabelIdType, typename EvalReader<ElemType>::LabelType> labelMap;
        return labelMap;
    }

    // SetLabelMapping - Sets the label mapping from integer index to label 
    // labelMapping - mapping table from label values to IDs (must be 0-n)
    // note: for tasks with labels, the mapping table must be the same between a training run and a testing run 
    virtual void SetLabelMapping(const std::wstring& /*sectionName*/, const std::map<typename EvalReader<ElemType>::LabelIdType, typename EvalReader<ElemType>::LabelType>& /*labelMapping*/) {}

    // GetData - Gets metadata from the specified section (into CPU memory) 
    // sectionName - section name to retrieve data from
    // numRecords - number of records to read
    // data - pointer to data buffer, if NULL, dataBufferSize will be set to size of required buffer to accomidate request
    // dataBufferSize - [in] size of the databuffer in bytes
    //                  [out] size of buffer filled with data
    // recordStart - record to start reading from, defaults to zero (start of data)
    // returns: true if data remains to be read, false if the end of data was reached
    virtual bool GetData(const std::wstring& /*sectionName*/, size_t /*numRecords*/, void* /*data*/, size_t& /*dataBufferSize*/, size_t /*recordStart=0*/) 
    {
        return false;
    }

    virtual bool DataEnd(EndDataType /*endDataType*/)
    {
        return m_currentRecord < m_recordCount;
    }
};

}}}
