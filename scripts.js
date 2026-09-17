```javascript
const form = document.getElementById("employeeForm");

form.addEventListener("submit", function(event) {

    event.preventDefault();

    // Get the values entered by the user
    const name = document.getElementById("name").value.trim();
    const surname = document.getElementById("surname").value.trim();
    const employeeNumber = document.getElementById("employeeNumber").value.trim();

    // Error message areas
    const nameError = document.getElementById("nameError");
    const surnameError = document.getElementById("surnameError");
    const employeeNumberError = document.getElementById("employeeNumberError");
    const successMessage = document.getElementById("successMessage");

    // Clear previous messages
    nameError.textContent = "";
    surnameError.textContent = "";
    employeeNumberError.textContent = "";
    successMessage.textContent = "";

    let valid = true;

    // Check that name contains letters only
    if (name === "") {
        nameError.textContent = "Please enter your name.";
        valid = false;
    } 
    else if (!/^[A-Za-z]+$/.test(name)) {
        nameError.textContent = "Name must contain letters only.";
        valid = false;
    }

    // Check that surname contains letters only
    if (surname === "") {
        surnameError.textContent = "Please enter your surname.";
        valid = false;
    } 
    else if (!/^[A-Za-z]+$/.test(surname)) {
        surnameError.textContent = "Surname must contain letters only.";
        valid = false;
    }

    // Check that employee number contains numbers only
    if (employeeNumber === "") {
        employeeNumberError.textContent = "Please enter your employee number.";
        valid = false;
    } 
    else if (!/^[0-9]+$/.test(employeeNumber)) {
        employeeNumberError.textContent = "Employee number must contain numbers only.";
        valid = false;
    }

    // If everything is correct
    if (valid) {
        successMessage.textContent = "Employee information successfully submitted!";
    }

});
```
