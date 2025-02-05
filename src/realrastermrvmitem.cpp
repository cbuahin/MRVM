#include <include/stdafx.h>
#include <include/mrvm.h>
#include <QDebug>

RealRaster::RealRaster(MRVMItem::IOType iotype, const QString &name)
    :RealArrayMRVMItem(iotype, name), RasterItem()
{
    m_properties["IncludeLocation"] = true;

    m_validCell = NULL;
    m_driver = NULL;
}

RealRaster::~RealRaster()
{
    if(m_validCell)
        delete[] m_validCell;

    m_validCell = NULL;

    /*    if(m_driver)
        delete m_driver*/;

    //    m_driver = NULL;
}

QString RealRaster::getName() const
{
    return m_name;
}

af::array RealRaster::trainingValues(int valueIndex, int startRow, int length)
{
    if(m_useRasterBootstrap)
    {
        return readTrainingDataFromSampler(m_trainingValuesAsString[valueIndex] , startRow, length);
    }
    else
    {
        return readDataFromRaster(m_trainingValuesAsString[valueIndex] , startRow , length);
    }
}

void RealRaster::setTrainingValuesAsString(const QList<QString> &trainingValues)
{
    MRVMItem::setTrainingValuesAsString(trainingValues);
}

af::array RealRaster::forecastValues(int valueIndex, int startRow, int length)
{
    if(m_useRasterBootstrap)
    {
        return readForecastDataFromSampler(m_forecastValuesAsString[valueIndex], startRow, length);
    }
    else
    {
        return readDataFromRaster(m_forecastValuesAsString[valueIndex], startRow, length);
    }

}

void RealRaster::setForecastValues(int row, const af::array& values, const af::array& uncertainty)
{
    //check if file exists. otherwise create.
    if(row < m_forecastValuesAsString.count())
    {
        QString filePath = m_forecastValuesAsString[row];
        writeDataToRaster(filePath, values);
    }

    //check if file exists. otherwise create.
    if(row < m_forecastUncertaintyValuesAsString.count())
    {
        QString filePath = m_forecastUncertaintyValuesAsString[row];
        writeDataToRaster(filePath, uncertainty);
    }
}

void RealRaster::setForecastValuesAsString(const QList<QString> &forecastValues)
{
    MRVMItem::setForecastValuesAsString(forecastValues);
}

void RealRaster::setForecastUncertaintyValueAsString(const QList<QString> &forecastUncertaintyValuesAsString)
{
    MRVMItem::setForecastUncertaintyValueAsString(forecastUncertaintyValuesAsString);
}

void RealRaster::readXML(QXmlStreamReader &xmlReader)
{
    MRVMItem::readXML(xmlReader);

    m_includeLocation =  m_properties["IncludeLocation"].toBool();

    readRasterProperties();

    if(m_iotype == MRVMItem::Output)
    {
        createOutputRasters();
    }
}

int RealRaster::numRowsPerTrainingValue() const
{
    return m_numRowsPerTrainingValue;
}

int RealRaster::numRowsPerForecastValue() const
{
    return m_numRowsPerForecastValue;
}

void RealRaster::resetProperties()
{

    if(m_useRasterBootstrap)
    {
        m_numRowsPerTrainingValue = m_sampleLocations.length();
        m_numRowsPerForecastValue = m_numValidPixels;
    }
    else
    {
        m_numRowsPerTrainingValue = m_numValidPixels;
        m_numRowsPerForecastValue = m_numValidPixels;
    }

    if(m_includeLocation)
    {
        m_columnCount = 3;
    }
    else
    {
        m_columnCount = 1;
    }
}

QString RealRaster::type() const
{
    return "RealRaster";
}

void RealRaster::writeDataToRaster(const QString& filePath, const af::array& valuesAf)
{
    //check if file exists. otherwise create.
    float* values = valuesAf.host<float>();

    GDALDataset* dataset = NULL;

    if(!QFile::exists(filePath) && m_iotype == MRVMItem::Output)
    {
        dataset = m_driver->Create(filePath.toStdString().c_str(), m_xSize , m_ySize, 1, GDT_Float32, NULL);
        dataset->SetGeoTransform(m_gcp);
        dataset->SetProjection(m_wktproj);
    }
    else
    {
        dataset = (GDALDataset*)GDALOpen(filePath.toStdString().c_str() , GA_Update);
    }

    if(dataset)
    {
        GDALRasterBand* dataBand = dataset->GetRasterBand(1);
        float * data = (float*) CPLMalloc(sizeof(float)*m_xSize*m_ySize);

        int count = 0;

        for(int i = 0 ; i < m_xSize ; i++)
        {
            for(int j = 0 ; j < m_ySize ; j++)
            {
                if(m_validCell[j * m_xSize + i])
                {
                    data[j*m_xSize + i] = values[count];
                    count++;
                }
                else
                {
                    data[j*m_xSize + i] = m_noData;
                }
            }
        }

        dataBand->RasterIO(GF_Write, 0,0,m_xSize , m_ySize, data, m_xSize , m_ySize , GDT_Float32, 0,0 );

        CPLFree(data);
        GDALClose(dataset);
        dataBand = NULL;
        dataset = NULL;
    }

    delete[] values;
}

af::array RealRaster::readDataFromRaster(const QString& filePath, int startRow, int length)
{
    af::array values(length,m_columnCount);

    if(QFile::exists(filePath) && m_numValidPixels)
    {
        GDALDataset* dataset = (GDALDataset*)GDALOpen(filePath.toStdString().c_str() , GA_ReadOnly);

        if(dataset)
        {
            GDALRasterBand* dataBand = dataset->GetRasterBand(1);
            float * data = (float*) CPLMalloc(sizeof(float)*m_xSize*m_ySize);

            dataBand->RasterIO(GF_Read, 0,0,m_xSize , m_ySize, data, m_xSize , m_ySize , GDT_Float32, 0,0 );

            int count = 0;

            if(m_includeLocation)
            {
                for(int i = 0 ; i < m_xSize ; i++)
                {
                    for(int j = 0 ; j < m_ySize ; j++)
                    {
                        if(m_validCell[j * m_xSize + i] )
                        {
                            if(count >= startRow && count < startRow + length)
                            {
                                QPointF loc = getCoordinates(i,j);
                                int rIndex = count - startRow;
                                values(rIndex,0) = data[j*m_xSize + i];
                                values(rIndex,1) = loc.x();
                                values(rIndex,2) = loc.y();
                            }

                            count++;
                        }
                    }
                }
            }
            else
            {
                for(int i = 0 ; i < m_xSize ; i++)
                {
                    for(int j = 0 ; j < m_ySize ; j++)
                    {
                        if(m_validCell[j * m_xSize + i])
                        {
                            if( count >= startRow && count < startRow + length)
                            {
                                values(count - startRow,0) = data[j*m_xSize + i];
                            }
                            count++;
                        }
                    }
                }
            }

            CPLFree(data);
            GDALClose(dataset);
            dataBand = NULL;
            dataset = NULL;

            return values;
        }
    }

    return values;
}

af::array RealRaster::readTrainingDataFromSampler(const QString& filePath, int startRow, int length)
{
    af::array values(length, m_columnCount);

    if(QFile::exists(filePath) && m_numValidPixels)
    {
        GDALDataset* dataset = (GDALDataset*)GDALOpen(filePath.toStdString().c_str() , GA_ReadOnly);

        if(dataset)
        {
            GDALRasterBand* dataBand = dataset->GetRasterBand(1);
            float * data = (float*) CPLMalloc(sizeof(float)*m_xSize*m_ySize);

            dataBand->RasterIO(GF_Read, 0,0,m_xSize , m_ySize, data, m_xSize , m_ySize , GDT_Float32, 0,0 );

            if(m_includeLocation)
            {
                for(int i = startRow ; i < startRow + length ; i++)
                {
                    QPoint p = m_sampleLocations[i];
                    QPointF pl = getCoordinates(p);
                    float cdata = data[p.y()*m_xSize + p.x()];
                    int rIndex = i - startRow;
                    values(rIndex,0) = cdata;
                    values(rIndex,1) = pl.x();
                    values(rIndex,2) = pl.y();
                }
            }
            else
            {
                for(int i = startRow ; i < startRow + length ; i++)
                {
                    QPoint p = m_sampleLocations[i];
                    float cdata = data[p.y()*m_xSize + p.x()];
                    values(i - startRow,0) = cdata;
                }
            }

            CPLFree(data);
            GDALClose(dataset);
            dataBand = NULL;
            dataset = NULL;

            return values;
        }
    }

    return values;
}

af::array RealRaster::readForecastDataFromSampler(const QString& filePath, int startRow, int length)
{
    af::array values(length, m_columnCount);

    if(QFile::exists(filePath) && m_numValidPixels)
    {
        GDALDataset* dataset = (GDALDataset*)GDALOpen(filePath.toStdString().c_str() , GA_ReadOnly);

        if(dataset)
        {
            GDALRasterBand* dataBand = dataset->GetRasterBand(1);
            float * data = (float*) CPLMalloc(sizeof(float)*m_xSize*m_ySize);

            dataBand->RasterIO(GF_Read, 0, 0, m_xSize, m_ySize, data, m_xSize, m_ySize, GDT_Float32, 0, 0);

            if(m_includeLocation)
            {
                int count = 0;

                for(int i = 0 ; i < m_xSize ; i++)
                {
                    for(int j = 0 ; j < m_ySize ; j++)
                    {
                        if(m_validCell[j * m_xSize + i])
                        {
                            if(count >= startRow && count < startRow + length)
                            {
                                QPointF p = getCoordinates(i,j);
                                int rIndex = count - startRow;
                                values(rIndex,0) = data[j*m_xSize + i];
                                values(rIndex,1) = p.x();
                                values(rIndex,2) = p.y();
                            }
                            count++;
                        }
                    }
                }
            }
            else
            {
                int count = 0;

                for(int i = 0 ; i < m_xSize ; i++)
                {
                    for(int j = 0 ; j < m_ySize ; j++)
                    {
                        if(m_validCell[j * m_xSize + i])
                        {
                            if(count >= startRow && count < startRow + length)
                            {
                                values(count - startRow,0) = data[j*m_xSize + i];
                            }

                            count++;
                        }
                    }
                }
            }

            CPLFree(data);
            GDALClose(dataset);
            dataBand = NULL;
            dataset = NULL;

            return values;
        }
    }

    return values;
}

void RealRaster::readRasterProperties()
{
    if(m_trainingValuesAsString.count() > 0)
    {

        GDALDataset * dataset = (GDALDataset*)GDALOpen(m_trainingValuesAsString[0].toStdString().c_str(), GA_ReadOnly);
        m_driver =  dataset->GetDriver();
        dataset->GetGeoTransform(m_gcp);

        if(dataset)
        {
            ASSERT(dataset->GetRasterCount() > 0,"");

            GDALRasterBand* rasterBand =  dataset->GetRasterBand(1);

            qDebug() << "Raster Type" << rasterBand->GetRasterDataType() ;

            m_xSize = dataset->GetRasterXSize();
            m_ySize = dataset->GetRasterYSize();
            m_noData = rasterBand->GetNoDataValue();
            m_wktproj = dataset->GetGCPProjection();

            float * data = (float*) CPLMalloc(sizeof(float)*m_xSize*m_ySize);

            rasterBand->RasterIO( GF_Read, 0,0,m_xSize , m_ySize, data, m_xSize , m_ySize , GDT_Float32, 0,0 );

            if(m_validCell)
            {
                delete[] m_validCell;
                m_validCell = NULL;
            }

            m_validCell = new int[m_xSize *  m_ySize];
            m_numValidPixels = 0;

            for(int i = 0 ; i < m_xSize ; i++)
            {
                for(int j = 0 ; j < m_ySize ; j++)
                {
                    if(data[j * m_xSize + i] == m_noData)
                    {
                        m_validCell[j*m_xSize + i] = 0;
                    }
                    else
                    {
                        m_validCell[j*m_xSize + i] = 1;
                        m_numValidPixels++;
                    }
                }
            }


            CPLFree(data);
            GDALClose(dataset);

            rasterBand = NULL;
            dataset = NULL;

            QVector<QPointF> bounds;

            bounds.append(getCoordinates(QPoint(0,0)).toPoint());
            bounds.append(getCoordinates(QPoint(m_xSize-1,0)).toPoint());
            bounds.append(getCoordinates(QPoint(0,m_ySize -1)).toPoint());
            bounds.append(getCoordinates(QPoint(m_xSize,m_ySize -1)).toPoint());

            m_boundary = QPolygonF(bounds);

            resetProperties();
        }
    }
}

void RealRaster::createOutputRasters()
{
    if(m_driver)
    {
        for(int i = 0 ; i < m_forecastValuesAsString.count() ; i++)
        {
            GDALDataset* newData = m_driver->Create(m_forecastValuesAsString[i].toStdString().c_str() ,m_xSize , m_ySize , 1, GDT_CFloat32, NULL);
            newData->SetProjection(m_wktproj);
            newData->SetGeoTransform(m_gcp);
            GDALRasterBand* newBand = newData->GetRasterBand(1);
            newBand->SetNoDataValue(m_noData);
            GDALClose(newData);
        }

        for(int i = 0 ; i < m_forecastUncertaintyValuesAsString.count() ; i++)
        {
            GDALDataset* newData = m_driver->Create(m_forecastUncertaintyValuesAsString[i].toStdString().c_str() ,m_xSize , m_ySize , 1, GDT_CFloat32,NULL);
            newData->SetProjection(m_wktproj);
            newData->SetGeoTransform(m_gcp);
            GDALRasterBand* newBand = newData->GetRasterBand(1);
            newBand->SetNoDataValue(m_noData);
            GDALClose(newData);
        }
    }
}

