//##########################################################################
//#                                                                        #
//#                              CLOUDCOMPARE                              #
//#                                                                        #
//#  This program is free software; you can redistribute it and/or modify  #
//#  it under the terms of the GNU General Public License as published by  #
//#  the Free Software Foundation; version 2 or later of the License.      #
//#                                                                        #
//#  This program is distributed in the hope that it will be useful,       #
//#  but WITHOUT ANY WARRANTY; without even the implied warranty of        #
//#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
//#  GNU General Public License for more details.                          #
//#                                                                        #
//#          COPYRIGHT: EDF R&D / TELECOM ParisTech (ENST-TSI)             #
//#                                                                        #
//##########################################################################

#include "FileIOFilter.h"

//CLOUDS
#include "AsciiFilter.h"
#include "BinFilter.h"

//MESHES
#include "PlyFilter.h"

//OTHERS
#include "DepthMapFileFilter.h"
#include "DxfFilter.h"
#include "ImageFileFilter.h"
#include "RasterGridFilter.h"
#include "ShpFilter.h"

//Qt
#include <QFileInfo>

#ifdef USE_VLD
//VLD
#include <vld.h>
#endif

//system
#include <cassert>
#include <vector>

//! Available filters
/** Filters are uniquely recognized by their 'file filter' string.
	We use a std::vector so as to keep the insertion ordering!
**/
static FileIOFilter::FilterContainer s_ioFilters;

static unsigned s_sessionCounter = 0;

void FileIOFilter::ResetSesionCounter()
{
	s_sessionCounter = 0;
}

unsigned FileIOFilter::IncreaseSesionCounter()
{
	return ++s_sessionCounter;
}

void FileIOFilter::InitInternalFilters()
{
	//from the most useful to the less one!
	Register(Shared(new BinFilter()));
	Register(Shared(new AsciiFilter()));

	Register(Shared(new PlyFilter()));

#ifdef CC_DXF_SUPPORT
	Register(Shared(new DxfFilter()));
#endif
#ifdef CC_SHP_SUPPORT
	Register(Shared(new ShpFilter()));
#endif
#ifdef CC_GDAL_SUPPORT
	Register(Shared(new RasterGridFilter()));
#endif
	Register(Shared(new ImageFileFilter()));
	Register(Shared(new DepthMapFileFilter()));
}

void FileIOFilter::Register(Shared filter)
{
	if (!filter)
	{
		assert(false);
		return;
	}

	//filters are uniquely recognized by their 'file filter' string
	const QStringList fileFilters = filter->getFileFilters(true);
	const QString filterName = filter->getDefaultExtension().toUpper();
	for (FilterContainer::const_iterator it=s_ioFilters.begin(); it!=s_ioFilters.end(); ++it)
	{
		bool error = false;
		if (*it == filter)
		{
			ccLog::Warning(QStringLiteral("[FileIOFilter::Register] I/O filter '%1' is already registered").arg(filterName));
			error = true;
		}
		else
		{
			//we are going to compare the file filters as they should remain unique!
			const QStringList otherFilters = (*it)->getFileFilters(true);
			for (int i=0; i<fileFilters.size(); ++i)
			{
				if (otherFilters.contains(fileFilters[i]))
				{
					const QString otherFilterName = (*it)->getDefaultExtension().toUpper();;
					ccLog::Warning(QStringLiteral("[FileIOFilter::Register] Internal error: file filter '%1' of filter '%2' is already handled by another filter ('%3')!").arg(fileFilters[i],filterName,otherFilterName));
					error = true;
					break;
				}
			}
		}

		if (error)
			return;
	}

	s_ioFilters.push_back(filter);
}

void FileIOFilter::UnregisterAll()
{
	for (auto & filter : s_ioFilters)
	{
		filter->unregister();
	}
	
	s_ioFilters.clear();
}

FileIOFilter::Shared FileIOFilter::GetFilter(const QString& fileFilter, bool onImport)
{
	if (!fileFilter.isEmpty())
	{
		for (FilterContainer::const_iterator it=s_ioFilters.begin(); it!=s_ioFilters.end(); ++it)
		{
			QStringList otherFilters = (*it)->getFileFilters(onImport);
			if (otherFilters.contains(fileFilter))
				return *it;
		}
	}

	return Shared(nullptr);
}

const FileIOFilter::FilterContainer& FileIOFilter::GetFilters()
{
	return s_ioFilters;
}

FileIOFilter::Shared FileIOFilter::FindBestFilterForExtension(const QString& ext)
{
	const QString upperExt = ext.toUpper();

	for (FilterContainer::const_iterator it=s_ioFilters.begin(); it!=s_ioFilters.end(); ++it)
	{
		if ((*it)->canLoadExtension(upperExt))
			return *it;
	}

	return Shared(nullptr);
}

ccHObject* FileIOFilter::LoadFromFile(	const QString& filename,
										LoadParameters& loadParameters,
										Shared filter,
										CC_FILE_ERROR& result)
{
	if (!filter)
	{
		ccLog::Error(QString("[Load] Internal error (invalid input filter)").arg(filename));
		result = CC_FERR_CONSOLE_ERROR;
		assert(false);
		return nullptr;
	}

	//check file existence
	QFileInfo fi(filename);
	if (!fi.exists())
	{
		ccLog::Error(QString("[Load] File '%1' doesn't exist!").arg(filename));
		result = CC_FERR_CONSOLE_ERROR; 
		return nullptr;
	}

	//load file
	ccHObject* container = new ccHObject();
	result = CC_FERR_NO_ERROR;
	
	//we start a new 'action' inside the current sessions
	unsigned sessionCounter = IncreaseSesionCounter();
	loadParameters.sessionStart = (sessionCounter == 1);

	try
	{
		result = filter->loadFile(	filename,
									*container,
									loadParameters);
	}
	catch (const std::exception& e)
	{
		ccLog::Warning(QString("[I/O] CC has caught an exception while loading file '%1'").arg(filename));
		ccLog::Warning(QString("[I/O] Exception: %1").arg(e.what()));
		if (container)
		{
			container->removeAllChildren();
		}
		result = CC_FERR_CONSOLE_ERROR;
	}
	catch (...)
	{
		ccLog::Warning(QString("[I/O] CC has caught an unhandled exception while loading file '%1'").arg(filename));
		if (container)
		{
			container->removeAllChildren();
		}
		result = CC_FERR_CONSOLE_ERROR;
	}

	if (result == CC_FERR_NO_ERROR)
	{
		ccLog::Print(QString("[I/O] File '%1' loaded successfully").arg(filename));
	}
	else
	{
		DisplayErrorMessage(result, "loading", fi.baseName());
	}

	unsigned childCount = container->getChildrenNumber();
	if (childCount != 0)
	{
		//we set the main container name as the full filename (with path)
		container->setName(QString("%1 (%2)").arg(fi.fileName(),fi.absolutePath()));
		for (unsigned i = 0; i < childCount; ++i)
		{
			ccHObject* child = container->getChild(i);
			QString newName = child->getName();
			if (newName.startsWith("unnamed"))
			{
				//we automatically replace occurrences of 'unnamed' in entities names by the base filename (no path, no extension)
				newName.replace(QString("unnamed"), fi.baseName());
				child->setName(newName);
			}
		}
	}
	else
	{
		delete container;
		container = nullptr;
	}

	return container;
}

ccHObject* FileIOFilter::LoadFromFile(	const QString& filename,
										LoadParameters& loadParameters,
										CC_FILE_ERROR& result,
										const QString& fileFilter )
{
	Shared filter(nullptr);
	
	//if the right filter is specified by the caller
	if (!fileFilter.isEmpty())
	{
		filter = GetFilter(fileFilter, true);
		if (!filter)
		{
			ccLog::Error(QString("[Load] Internal error: no I/O filter corresponds to filter '%1'").arg(fileFilter));
			result = CC_FERR_CONSOLE_ERROR;
			return nullptr;
		}
	}
	else //we need to guess the I/O filter based on the file format
	{
		//look for file extension (we trust Qt on this task)
		QString extension = QFileInfo(filename).suffix();
		if (extension.isEmpty())
		{
			ccLog::Error("[Load] Can't guess file format: no file extension");
			result = CC_FERR_CONSOLE_ERROR;
			return nullptr;
		}

		//convert extension to file format
		filter = FindBestFilterForExtension(extension);

		//unknown extension?
		if (!filter)
		{
			ccLog::Error(QString("[Load] Can't guess file format: unhandled file extension '%1'").arg(extension));
			result = CC_FERR_CONSOLE_ERROR;
			return nullptr;
		}
	}

	return LoadFromFile(filename, loadParameters, filter, result);
}

CC_FILE_ERROR FileIOFilter::SaveToFile(	ccHObject* entities,
										const QString& filename,
										const SaveParameters& parameters,
										Shared filter)
{
	if (!entities || filename.isEmpty() || !filter)
		return CC_FERR_BAD_ARGUMENT;

	//if the file name has no extension, we had a default one!
	QString completeFileName(filename);
	if (QFileInfo(filename).suffix().isEmpty())
		completeFileName += QString(".%1").arg(filter->getDefaultExtension());

	CC_FILE_ERROR result = CC_FERR_NO_ERROR;
	try
	{
		result = filter->saveToFile(entities, completeFileName, parameters);
	}
	catch(...)
	{
		ccLog::Warning(QString("[I/O] CC has caught an unhandled exception while saving file '%1'").arg(filename));
		result = CC_FERR_CONSOLE_ERROR;
	}

	if (result == CC_FERR_NO_ERROR)
	{
		ccLog::Print(QString("[I/O] File '%1' saved successfully").arg(filename));
	}
	else
	{
		DisplayErrorMessage(result,"saving",filename);
	}

	return result;
}

CC_FILE_ERROR FileIOFilter::SaveToFile(	ccHObject* entities,
										const QString& filename,
										const SaveParameters& parameters,
										const QString& fileFilter)
{
	if (fileFilter.isEmpty())
		return CC_FERR_BAD_ARGUMENT;

	Shared filter = GetFilter(fileFilter,false);
	if (!filter)
	{
		ccLog::Error(QString("[Load] Internal error: no filter corresponds to filter '%1'").arg(fileFilter));
		return CC_FERR_UNKNOWN_FILE;
	}

	return SaveToFile(entities, filename, parameters, filter);
}

void FileIOFilter::DisplayErrorMessage(CC_FILE_ERROR err, const QString& action, const QString& filename)
{
	QString errorStr;

	bool warning = false;
	switch(err)
	{
	case CC_FERR_NO_ERROR:
		return; //no message will be displayed!
	case CC_FERR_BAD_ARGUMENT:
		errorStr = "bad argument (internal)";
		break;
	case CC_FERR_UNKNOWN_FILE:
		errorStr = "unknown file";
		break;
	case CC_FERR_WRONG_FILE_TYPE:
		errorStr = "wrong file type (check header)";
		break;
	case CC_FERR_WRITING:
		errorStr = "writing error (disk full/no access right?)";
		break;
	case CC_FERR_READING:
		errorStr = "reading error (no access right?)";
		break;
	case CC_FERR_NO_SAVE:
		errorStr = "nothing to save";
		break;
	case CC_FERR_NO_LOAD:
		errorStr = "nothing to load";
		break;
	case CC_FERR_BAD_ENTITY_TYPE:
		errorStr = "incompatible entity/file types";
		break;
	case CC_FERR_CANCELED_BY_USER:
		errorStr = "process canceled by user";
		warning = true;
		break;
	case CC_FERR_NOT_ENOUGH_MEMORY:
		errorStr = "not enough memory";
		break;
	case CC_FERR_MALFORMED_FILE:
		errorStr = "malformed file";
		break;
	case CC_FERR_BROKEN_DEPENDENCY_ERROR:
		errorStr = "dependent entities missing (see Console)";
		break;
	case CC_FERR_FILE_WAS_WRITTEN_BY_UNKNOWN_PLUGIN:
		errorStr = "the file was written by a plugin but none of the loaded plugins can deserialize it";
		break;
	case CC_FERR_THIRD_PARTY_LIB_FAILURE:
		errorStr = "the third-party library in charge of saving/loading the file has failed to perform the operation";
		break;
	case CC_FERR_THIRD_PARTY_LIB_EXCEPTION:
		errorStr = "the third-party library in charge of saving/loading the file has thrown an exception";
		break;
	case CC_FERR_NOT_IMPLEMENTED:
		errorStr = "this function is not implemented yet!";
		break;
	case CC_FERR_CONSOLE_ERROR:
		errorStr = "see console";
		break;
	default:
		return; //no message will be displayed!
	}

	QString outputString = QString("An error occurred while %1 '%2': ").arg(action,filename) + errorStr;
	if (warning)
		ccLog::Warning(outputString);
	else
		ccLog::Error(outputString);
}

bool FileIOFilter::CheckForSpecialChars(const QString& filename)
{
	return (filename.normalized(QString::NormalizationForm_D) != filename);
}

bool FileIOFilter::HandleGlobalShift(	const CCVector3d& P,
										CCVector3d& Pshift,
										bool& preserveCoordinateShift,
										LoadParameters& loadParameters,
										bool useInputCoordinatesShiftIfPossible/*=false*/)
{
	bool shiftAlreadyEnabled = (loadParameters.coordinatesShiftEnabled && *loadParameters.coordinatesShiftEnabled && loadParameters.coordinatesShift);
	if (shiftAlreadyEnabled)
	{
		Pshift = *loadParameters.coordinatesShift;
		preserveCoordinateShift = loadParameters.preserveShiftOnSave;
	}
	
	bool applyAll = false;
	if (	sizeof(PointCoordinateType) < 8
		&&	ccGlobalShiftManager::Handle(	P,
											0,
											loadParameters.shiftHandlingMode,
											shiftAlreadyEnabled || useInputCoordinatesShiftIfPossible,
											Pshift,
											&preserveCoordinateShift,
											nullptr,
											&applyAll) )
	{
		//we save coordinates shift information
		if (applyAll && loadParameters.coordinatesShiftEnabled && loadParameters.coordinatesShift)
		{
			*loadParameters.coordinatesShiftEnabled = true;
			*loadParameters.coordinatesShift = Pshift;
			loadParameters.preserveShiftOnSave = preserveCoordinateShift;
		}

		return true;
	}

	return false;
}
