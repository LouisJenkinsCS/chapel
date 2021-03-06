/*
  Holder for test result information.
  Test results are automatically managed by the TestLauncher, and do not 
  need to be explicitly manipulated by writers of tests.  
  Each instance holds the total number of tests run, and collections of 
  failures and errors that occurred among those test runs. The collections
  contain tuples of (testcase, exceptioninfo), where exceptioninfo is the
  formatted traceback of the error that occurred.
*/
module TestResult {
  class TestResult {
    type tupType = 2*string;
    var failures: [1..0] tupType,
        errors: [1..0] tupType,
        skipped: [1..0] tupType;
    var testsRun = 0;
    var shouldStop = false;
    var separator1 = "="* 70,
        separator2 = "-"* 70;
    // called when a test ran
    proc testRan() {
      this.testsRun += 1;
    }
    
    /*Called when an error has occurred.*/
    proc addError(testName: string, fileName: string, errMsg: string) {
      this.testRan();
      var fileAdd = fileName + ": " + testName;
      this.errors.push_back((fileAdd, errMsg));
    }

    /*called when error occured */
    proc addFailure(testName: string, fileName: string, errMsg: string) {
      this.testRan();
      var fileAdd = fileName + ": " + testName;
      this.failures.push_back((fileAdd, errMsg));
    }

    /*Called when a test has completed successfully*/
    proc addSuccess(testName: string, fileName: string) {
      this.testRan();
    }

    /*Called when a test is skipped.*/
    proc addSkip(testName: string, fileName: string, errMsg: string) {
      this.testRan();
      var fileAdd = fileName + ": " + testName;
      this.skipped.push_back((fileAdd, errMsg));
    }

    /*Tells whether or not this result was a success.*/
    proc wasSuccessful() {
      return this.failures.size == 0 && this.errors.size == 0;
    }
    
    /* Indicates that the tests should be aborted. */
    proc stop() {
      this.shouldStop = true;
    }

    /*Count of test skipped*/
    proc numSkippedTests() {
      return this.skipped.size;
    }

    /*Count of test failed*/
    proc numFailedTests() {
      return this.failures.size;
    }

    /*Count of tests giving error*/
    proc numErroredTests() {
      return this.errors.size;
    }

    proc printErrors() {
      writeln();
      this.printErrorList("ERROR", this.errors);
      this.printErrorList("FAIL", this.failures);
      this.printErrorList("SKIPPED", this.skipped);
    }

    proc printErrorList(flavour, errors) {
      for (test, err) in errors {
        writeln(this.separator1);
        writeln(flavour, " ", test);
        writeln(this.separator2);
        writeln(err);
      }
    }

    /* Function to print the result*/
    proc printResult() {
      var skipped = this.numSkippedTests();
      var run = this.testsRun - skipped;
      if this.testsRun != 0 {
        writeln("Run "+ run +" "+ printTest(run));
        writeln();
        var infos: [1..0](string);
        if !this.wasSuccessful() {
          write("FAILED");
          var failed = this.numFailedTests(),
            errored = this.numErroredTests();
          if failed then
            infos.push_back("failures = " + failed);
          if errored then
            infos.push_back("errors = " + errored);
        }
        else
          write("OK");
        if skipped then
          infos.push_back("skipped = " + skipped);
        if infos.size {
          write(" ");
          for info in infos do write(info, " ");
        }
        write("\n");
      }
      else {
        writeln("No Tests Found");
      }
    }

    proc printTest(count) {
      if count > 1 {
        return "tests";
      }
      return "test";
    }
  }
}