/**
 * Example: How to use the Profile Comparison feature
 * 
 * This example demonstrates how to upload a 3MF file and compare its parameters
 * with a selected printer profile to identify differences and compatibility.
 */

const fs = require('fs')
const path = require('path')
const FormData = require('form-data')
const axios = require('axios')

// Configuration
const API_BASE_URL = 'http://localhost:3030'
const TEST_FILE_PATH = path.join(__dirname, '../../test_files/test_3mf.3mf')

async function demonstrateProfileComparison() {
  console.log('🔍 Profile Comparison Example')
  console.log('=' .repeat(50))

  try {
    // Step 1: Get available printer profiles
    console.log('\n1. Fetching available printer profiles...')
    const profilesResponse = await axios.get(`${API_BASE_URL}/printer-profiles`)
    const profiles = profilesResponse.data
    
    console.log(`Found ${profiles.length} printer profiles:`)
    profiles.forEach((profile, index) => {
      console.log(`  ${index + 1}. ${profile.text} (ID: ${profile.id})`)
    })

    if (profiles.length === 0) {
      console.log('❌ No printer profiles found. Please add some profiles first.')
      return
    }

    // Step 2: Select a profile for comparison (use the first one)
    const selectedProfile = profiles[0]
    console.log(`\n2. Selected profile for comparison: ${selectedProfile.text}`)

    // Step 3: Upload 3MF file with comparison enabled
    console.log('\n3. Uploading 3MF file with profile comparison...')
    
    const formData = new FormData()
    formData.append('file', fs.createReadStream(TEST_FILE_PATH))

    const uploadResponse = await axios.post(
      `${API_BASE_URL}/files/info?includeComparison=true&compareWithProfile=${selectedProfile.id}`,
      formData,
      {
        headers: {
          ...formData.getHeaders(),
        },
      }
    )

    const result = uploadResponse.data
    
    // Step 4: Display extraction results
    console.log('\n4. File Analysis Results:')
    console.log('   📄 File Parameters:')
    console.log(`      Printer: ${result.printer || 'Not detected'}`)
    console.log(`      Nozzle: ${result.nozzle || 'Not detected'}`)
    console.log(`      Profile: ${result.profile || 'Not detected'}`)
    console.log(`      Technical Name: ${result.technicalName || 'Not detected'}`)

    // Step 5: Display comparison results
    if (result.profileComparison) {
      console.log('\n5. Profile Comparison Results:')
      console.log(`   🎯 Compared with: ${selectedProfile.text}`)
      console.log(`   📊 Compatibility Score: ${result.profileComparison.summary.compatibilityScore}%`)
      console.log(`   📈 Total Differences: ${result.profileComparison.summary.totalDifferences}`)
      console.log(`   ⚠️  Critical Differences: ${result.profileComparison.summary.criticalDifferences}`)

      if (result.profileComparison.differences.length > 0) {
        console.log('\n   📋 Parameter Differences:')
        result.profileComparison.differences.forEach((diff, index) => {
          console.log(`      ${index + 1}. ${diff.parameter}:`)
          console.log(`         File Value: ${diff.fileValue || 'null'}`)
          console.log(`         Profile Value: ${diff.profileValue || 'null'}`)
        })
      } else {
        console.log('\n   ✅ No differences found - Perfect match!')
      }

      // Step 6: Provide recommendations
      console.log('\n6. Recommendations:')
      const score = result.profileComparison.summary.compatibilityScore
      const criticalDiffs = result.profileComparison.summary.criticalDifferences

      if (score >= 90) {
        console.log('   ✅ Excellent compatibility - Safe to print')
      } else if (score >= 70) {
        console.log('   ⚠️  Good compatibility - Minor adjustments may be needed')
      } else if (score >= 50) {
        console.log('   ⚠️  Moderate compatibility - Review differences carefully')
      } else {
        console.log('   ❌ Low compatibility - Significant adjustments required')
      }

      if (criticalDiffs > 0) {
        console.log('   🚨 Critical differences detected - Review printer model and nozzle diameter')
      }

    } else {
      console.log('\n5. ❌ Profile comparison was not performed')
    }

  } catch (error) {
    console.error('\n❌ Error during demonstration:', error.message)
    
    if (error.response) {
      console.error('Response status:', error.response.status)
      console.error('Response data:', error.response.data)
    }
  }
}

// Example usage scenarios
async function showUsageScenarios() {
  console.log('\n\n📚 Usage Scenarios')
  console.log('=' .repeat(50))

  console.log(`
1. 🎯 Quality Control:
   - Upload customer files before printing
   - Compare with your printer profiles
   - Identify potential issues early

2. 🔧 Profile Validation:
   - Verify file compatibility with available printers
   - Check for critical parameter mismatches
   - Ensure optimal print quality

3. 📊 Batch Processing:
   - Process multiple files automatically
   - Generate compatibility reports
   - Sort files by compatibility score

4. 🚀 API Integration:
   - Integrate with existing workflows
   - Automate file validation
   - Build custom dashboards

Example API calls:

// Basic file analysis (no comparison)
POST /files/info
Content-Type: multipart/form-data
Body: file=@your-file.3mf

// File analysis with profile comparison
POST /files/info?includeComparison=true&compareWithProfile=profile-id
Content-Type: multipart/form-data
Body: file=@your-file.3mf

// Get available profiles for comparison
GET /printer-profiles
`)
}

// Run the demonstration
if (require.main === module) {
  demonstrateProfileComparison()
    .then(() => showUsageScenarios())
    .then(() => {
      console.log('\n✅ Demonstration completed!')
      console.log('💡 Start the API server with "npm start" to try these examples')
    })
    .catch(console.error)
}

module.exports = {
  demonstrateProfileComparison,
  showUsageScenarios
}
