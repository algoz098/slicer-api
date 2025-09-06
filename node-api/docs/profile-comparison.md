# Profile Comparison Feature

## Overview

The Profile Comparison feature allows you to compare parameters extracted from uploaded 3D printing files (3MF) with existing printer profiles to identify differences and assess compatibility.

## Features

- **Parameter Extraction**: Automatically extracts printer model, nozzle diameter, profile name, and technical name from 3MF files
- **Profile Comparison**: Compares file parameters with selected printer profiles
- **Compatibility Scoring**: Provides a 0-100% compatibility score
- **Difference Detection**: Identifies specific parameter differences
- **Critical Analysis**: Highlights critical differences that may affect print quality

## API Usage

### Basic File Analysis

```http
POST /files/info
Content-Type: multipart/form-data

file=@your-file.3mf
```

**Response:**
```json
{
  "printer": "Bambu Lab X1 Carbon",
  "nozzle": "0.4",
  "technicalName": "@BBL X1C",
  "profile": "0.20mm Standard @BBL X1C"
}
```

### File Analysis with Profile Comparison

```http
POST /files/info?includeComparison=true&compareWithProfile=profile-id
Content-Type: multipart/form-data

file=@your-file.3mf
```

**Response:**
```json
{
  "printer": "Bambu Lab X1 Carbon",
  "nozzle": "0.4",
  "technicalName": "@BBL X1C",
  "profile": "0.20mm Standard @BBL X1C",
  "profileComparison": {
    "selectedProfileId": "profile-id",
    "differences": [
      {
        "parameter": "Nozzle Diameter",
        "fileValue": "0.4",
        "profileValue": "0.6"
      }
    ],
    "summary": {
      "totalDifferences": 1,
      "criticalDifferences": 1,
      "compatibilityScore": 75
    }
  }
}
```

## Query Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `includeComparison` | boolean | Enable profile comparison (default: false) |
| `compareWithProfile` | string | ID of printer profile to compare against |

## Response Schema

### FilesInfo Object

| Field | Type | Description |
|-------|------|-------------|
| `printer` | string | Printer model detected from file |
| `nozzle` | string | Nozzle diameter (e.g., "0.4") |
| `technicalName` | string | Technical profile identifier |
| `profile` | string | Print profile name |
| `profileComparison` | object | Comparison results (optional) |

### ProfileComparison Object

| Field | Type | Description |
|-------|------|-------------|
| `selectedProfileId` | string | ID of profile used for comparison |
| `differences` | array | List of parameter differences |
| `summary` | object | Comparison summary statistics |

### Difference Object

| Field | Type | Description |
|-------|------|-------------|
| `parameter` | string | Name of the differing parameter |
| `fileValue` | any | Value from the uploaded file |
| `profileValue` | any | Value from the printer profile |

### Summary Object

| Field | Type | Description |
|-------|------|-------------|
| `totalDifferences` | number | Total number of differences found |
| `criticalDifferences` | number | Number of critical differences |
| `compatibilityScore` | number | Compatibility percentage (0-100) |

## Compatibility Scoring

The compatibility score is calculated based on parameter matches:

- **90-100%**: Excellent compatibility - Safe to print
- **70-89%**: Good compatibility - Minor adjustments may be needed
- **50-69%**: Moderate compatibility - Review differences carefully
- **0-49%**: Low compatibility - Significant adjustments required

### Critical Parameters

Critical parameters have higher impact on compatibility scoring:

- **Printer Model**: Ensures hardware compatibility
- **Nozzle Diameter**: Affects extrusion and print quality

Critical differences reduce the compatibility score by 25 points each.

## Error Handling

### Common Errors

| Error | Description | Solution |
|-------|-------------|----------|
| `BadRequest: No file provided` | No file uploaded | Include file in multipart form data |
| `BadRequest: Unsupported file format` | Invalid file type | Use supported formats (3MF) |
| `NotFound: Profile not found` | Invalid profile ID | Use valid profile ID from `/printer-profiles` |
| `BadRequest: validation failed` | Invalid parameters | Check query parameters |

### Error Response Format

```json
{
  "name": "BadRequest",
  "message": "Detailed error message",
  "code": 400,
  "className": "bad-request",
  "errors": {}
}
```

## Integration Examples

### JavaScript/Node.js

```javascript
const FormData = require('form-data')
const axios = require('axios')
const fs = require('fs')

async function compareFile(filePath, profileId) {
  const formData = new FormData()
  formData.append('file', fs.createReadStream(filePath))

  const response = await axios.post(
    `http://localhost:3030/files/info?includeComparison=true&compareWithProfile=${profileId}`,
    formData,
    { headers: formData.getHeaders() }
  )

  return response.data
}
```

### Python

```python
import requests

def compare_file(file_path, profile_id):
    with open(file_path, 'rb') as f:
        files = {'file': f}
        params = {
            'includeComparison': 'true',
            'compareWithProfile': profile_id
        }
        
        response = requests.post(
            'http://localhost:3030/files/info',
            files=files,
            params=params
        )
        
        return response.json()
```

### cURL

```bash
curl -X POST \
  "http://localhost:3030/files/info?includeComparison=true&compareWithProfile=profile-id" \
  -H "Content-Type: multipart/form-data" \
  -F "file=@your-file.3mf"
```

## Use Cases

1. **Quality Control**: Validate files before printing
2. **Profile Management**: Ensure file-profile compatibility
3. **Batch Processing**: Process multiple files automatically
4. **Customer Service**: Provide compatibility feedback
5. **Workflow Integration**: Automate file validation pipelines

## Performance Considerations

- File processing time depends on file size and complexity
- Profile comparison adds minimal overhead (~10-50ms)
- Consider caching profile data for high-volume scenarios
- Use appropriate timeout values for large files

## Security Notes

- Files are temporarily stored during processing
- Automatic cleanup removes files after processing
- No persistent storage of uploaded files
- Profile data access follows service permissions
