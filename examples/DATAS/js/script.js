// When the button is clicked, change the bodyText text
document.getElementById('clickButton').addEventListener('click', () => {
	// Change the text of the header
	if( document.getElementById('bodyText').textContent == 'click !' )
	{
		document.getElementById('bodyText').textContent = 'clack !!';
	}
	else
	{
		document.getElementById('bodyText').textContent = 'click !';
	}
});

// When the button is clicked, change the colorButton color
document.getElementById('colorButton').addEventListener('click', function() {
	if( this.style.backgroundColor == 'blue' )
	{
		this.style.backgroundColor = 'red';
	}
	else
	{
		this.style.backgroundColor = 'blue';
	}
	this.textContent = 'Color Changed!';
	// Toggle image display
    var image = document.getElementById('image');
    if (image.style.display === 'none') {
        image.style.display = 'block';
    } else {
        image.style.display = 'none';
    }
});

